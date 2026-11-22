#include "route_control.h"

#include <stdio.h>
#include <string.h>

#include "cdc_bench.h"
#include "route_policy.h"
#include "route_store.h"
#include "third_party/embedded-cli/embedded_cli.h"
#include "usb_bridge.h"

#define RESPONSE_QUEUE_DEPTH 8u
#define RESPONSE_LINE_SIZE 112u
#define HUMAN_OUTPUT_CAPACITY 8192u
#define CLI_STORAGE_BYTES 4096u

typedef struct {
    char lines[RESPONSE_QUEUE_DEPTH][RESPONSE_LINE_SIZE];
    uint8_t head;
    uint8_t count;
} response_queue_t;

typedef enum { GET_IDLE = 0, GET_CONFIG, GET_DEVICES, GET_RULES, GET_END } get_state_t;

static bridge_t *active_bridge;
static route_policy_t staged_policy;
static bool transaction_active;
static bool transaction_poisoned;
static response_queue_t responses;
static get_state_t get_state;
static uint8_t get_device_id;
static uint8_t get_rule_index;

static char human_output[HUMAN_OUTPUT_CAPACITY];
static uint16_t human_head;
static uint16_t human_count;
static bool welcome_pending;
static bool cli_at_line_start = true;
static bool cli_pending_prompt;
static bool restart_notice_queued;
static const char *last_commit_error = "invalid";
static EmbeddedCli *cli;
static CLI_UINT cli_storage[BYTES_TO_CLI_UINTS(CLI_STORAGE_BYTES)];

static bool response_push(const char *line)
{
    if (responses.count == RESPONSE_QUEUE_DEPTH) return false;
    uint8_t index = (uint8_t)((responses.head + responses.count) % RESPONSE_QUEUE_DEPTH);
    size_t length = strlen(line);
    if (length >= RESPONSE_LINE_SIZE - 1u) return false;
    memcpy(responses.lines[index], line, length);
    responses.lines[index][length] = '\n';
    responses.lines[index][length + 1u] = '\0';
    ++responses.count;
    return true;
}

static void response_error(const char *reason)
{
    char line[RESPONSE_LINE_SIZE];
    (void)snprintf(line, sizeof(line), "R ERR %s", reason);
    (void)response_push(line);
}

static bool parse_u8(const char *text, uint8_t *value)
{
    if (text == NULL || *text == '\0') return false;
    unsigned result = 0;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        result = result * 10u + (unsigned)(*cursor - '0');
        if (result > UINT8_MAX) return false;
    }
    *value = (uint8_t)result;
    return true;
}

static bool parse_rule(const char *line, route_rule_t *rule)
{
    uint8_t *fields[] = {&rule->source_id, &rule->source_cable, &rule->channel,
                         &rule->action, &rule->destination_id,
                         &rule->destination_cable};
    const char *cursor = line + 7u;
    for (size_t index = 0; index < 6u; ++index) {
        if (*cursor == '\0' || *cursor == ' ') return false;
        unsigned result = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            result = result * 10u + (unsigned)(*cursor++ - '0');
            if (result > UINT8_MAX) return false;
        }
        if (cursor == line + 7u) return false;
        *fields[index] = (uint8_t)result;
        if (index != 5u) {
            if (*cursor != ' ') return false;
            while (*cursor == ' ') ++cursor;
        }
    }
    return *cursor == '\0';
}

static bool known_id(uint8_t id)
{
    return id != 0u && id <= ROUTE_STORE_MAX_ID &&
           route_store_device_name(id) != NULL;
}

static bool policy_refs_known_devices(const route_policy_t *policy)
{
    for (uint8_t index = 0; index < policy->count; ++index) {
        const route_rule_t *rule = &policy->rules[index];
        if (rule->source_id != 0u && !known_id(rule->source_id)) return false;
        if (rule->action == ROUTE_TO && !known_id(rule->destination_id)) return false;
    }
    return true;
}

static bool begin_policy(uint8_t default_action)
{
    if (get_state != GET_IDLE || transaction_active ||
        (default_action != ROUTE_ALL && default_action != ROUTE_DROP)) return false;
    memset(&staged_policy, 0, sizeof(staged_policy));
    staged_policy.default_action = default_action;
    transaction_active = true;
    transaction_poisoned = false;
    return true;
}

static bool stage_rule(const route_rule_t *rule)
{
    if (!transaction_active || transaction_poisoned || rule == NULL ||
        staged_policy.count == ROUTE_POLICY_MAX_RULES) {
        if (transaction_active) transaction_poisoned = true;
        return false;
    }
    route_policy_t candidate = staged_policy;
    candidate.rules[candidate.count++] = *rule;
    if (!route_policy_validate(&candidate) || !policy_refs_known_devices(&candidate)) {
        transaction_poisoned = true;
        return false;
    }
    staged_policy = candidate;
    return true;
}

static bool commit_policy(void)
{
    last_commit_error = "invalid";
    if (!transaction_active || transaction_poisoned ||
        !route_policy_validate(&staged_policy) ||
        !policy_refs_known_devices(&staged_policy)) {
        transaction_active = false;
        transaction_poisoned = false;
        return false;
    }
    if (!route_store_save_policy(&staged_policy)) {
        last_commit_error = route_store_last_error();
        transaction_active = false;
        transaction_poisoned = false;
        return false;
    }
    if (active_bridge != NULL && !bridge_set_policy(active_bridge, &staged_policy)) {
        last_commit_error = "apply";
        transaction_active = false;
        transaction_poisoned = false;
        return false;
    }
    transaction_active = false;
    transaction_poisoned = false;
    last_commit_error = "none";
    return true;
}

static void abort_policy(void)
{
    transaction_active = false;
    transaction_poisoned = false;
    memset(&staged_policy, 0, sizeof(staged_policy));
}

static bool enqueue_human_char(char character)
{
    if (human_count == HUMAN_OUTPUT_CAPACITY) return false;
    uint16_t index = (uint16_t)((human_head + human_count) % HUMAN_OUTPUT_CAPACITY);
    human_output[index] = character;
    ++human_count;
    return true;
}

static void cli_write_char(EmbeddedCli *embedded_cli, char character)
{
    (void)embedded_cli;
    if (cli_pending_prompt) {
        cli_pending_prompt = false;
        if (character == ' ') return;
        (void)enqueue_human_char('>');
    }
    if (cli_at_line_start && character == '>') {
        cli_pending_prompt = true;
        return;
    }
    (void)enqueue_human_char(character);
    if (character == '\n') cli_at_line_start = true;
    else if (character != '\r') cli_at_line_start = false;
}

static void cli_print(const char *text)
{
    if (cli != NULL) embeddedCliPrint(cli, text);
}

static bool cli_initialize(void);

static bool parse_device_token(const char *token, bool allow_any, uint8_t *id)
{
    if (token == NULL || id == NULL) return false;
    if (allow_any && strcmp(token, "any") == 0) {
        *id = 0;
        return true;
    }
    uint8_t numeric;
    if (parse_u8(token, &numeric)) {
        if (numeric <= ROUTE_STORE_MAX_ID && known_id(numeric)) {
            *id = numeric;
            return true;
        }
        return false;
    }
    for (uint8_t candidate = 1; candidate <= ROUTE_STORE_MAX_ID; ++candidate) {
        const char *name = route_store_device_name(candidate);
        if (name != NULL && strcmp(name, token) == 0) {
            *id = candidate;
            return true;
        }
    }
    return false;
}

static bool parse_cable_token(const char *token, uint8_t *cable)
{
    if (token != NULL && strcmp(token, "any") == 0) {
        *cable = ROUTE_ANY;
        return true;
    }
    return parse_u8(token, cable) && *cable <= 15u;
}

static bool parse_channel_token(const char *token, uint8_t *channel)
{
    if (token != NULL && strcmp(token, "any") == 0) {
        *channel = ROUTE_ANY;
        return true;
    }
    if (token != NULL && strcmp(token, "system") == 0) {
        *channel = ROUTE_SYSTEM_CHANNEL;
        return true;
    }
    uint8_t human_channel;
    if (!parse_u8(token, &human_channel) || human_channel < 1u || human_channel > 16u)
        return false;
    *channel = (uint8_t)(human_channel - 1u);
    return true;
}

static const char *device_name(uint8_t id, char *buffer, size_t capacity)
{
    const char *name = route_store_device_name(id);
    if (name != NULL) return name;
    (void)snprintf(buffer, capacity, "ID-%u", id);
    return buffer;
}

static void print_device_state(uint8_t id)
{
    const char *name = route_store_device_name(id);
    if (name == NULL) return;
    uint8_t endpoint = 0xffu;
    for (uint8_t index = 0; index < USB_BRIDGE_ENDPOINT_COUNT; ++index) {
        if (active_bridge != NULL && active_bridge->router.endpoints[index].route_id == id) {
            endpoint = index;
            break;
        }
    }
    bool online = endpoint != 0xffu && active_bridge->endpoints[endpoint].generation != 0u;
    char line[192];
    if (online) {
        (void)snprintf(line, sizeof(line), "ID %u %s online rx=%u tx=%u",
                       id, name, active_bridge->endpoints[endpoint].rx_cables,
                       active_bridge->endpoints[endpoint].tx_cables);
    } else {
        (void)snprintf(line, sizeof(line), "ID %u %s offline", id, name);
    }
    cli_print(line);
}

static void on_devices(EmbeddedCli *embedded_cli, char *args, void *context)
{
    (void)embedded_cli; (void)args; (void)context;
    cli_print("Logical MIDI devices (IDs are used by rule):");
    cli_print("ID 0 means any source; cable 0..15 or any; channel 1..16, system, or any.");
    for (uint8_t id = 1; id <= ROUTE_STORE_MAX_ID; ++id) print_device_state(id);
}

static const char *action_name(uint8_t action)
{
    switch (action) {
    case ROUTE_ALL: return "all";
    case ROUTE_DROP: return "drop";
    case ROUTE_TO: return "to";
    case ROUTE_EXCLUDE: return "exclude";
    default: return "invalid";
    }
}

static void on_rules(EmbeddedCli *embedded_cli, char *args, void *context)
{
    (void)embedded_cli; (void)args; (void)context;
    const route_policy_t *policy = route_store_policy();
    char line[192];
    (void)snprintf(line, sizeof(line), "Default: %s", action_name(policy->default_action));
    cli_print(line);
    if (policy->count == 0u) cli_print("No ordered rules; default applies.");
    for (uint8_t index = 0; index < policy->count; ++index) {
        const route_rule_t *rule = &policy->rules[index];
        char source_id_buffer[16], destination_id_buffer[16];
        const char *source = rule->source_id == 0u ? "any" :
                             device_name(rule->source_id, source_id_buffer, sizeof(source_id_buffer));
        const char *cable = rule->source_cable == ROUTE_ANY ? "any" : NULL;
        const char *channel = rule->channel == ROUTE_ANY ? "any" :
                              rule->channel == ROUTE_SYSTEM_CHANNEL ? "system" : NULL;
        char match_buffer[24];
        if (cable == NULL) {
            (void)snprintf(match_buffer, sizeof(match_buffer), "%u", rule->source_cable);
            cable = match_buffer;
        }
        char channel_buffer[24];
        if (channel == NULL) {
            (void)snprintf(channel_buffer, sizeof(channel_buffer), "%u", rule->channel + 1u);
            channel = channel_buffer;
        }
        if (rule->action == ROUTE_TO) {
            const char *destination = device_name(rule->destination_id, destination_id_buffer,
                                                 sizeof(destination_id_buffer));
            char destination_cable[24];
            if (rule->destination_cable == ROUTE_ANY) strcpy(destination_cable, "any");
            else (void)snprintf(destination_cable, sizeof(destination_cable), "%u",
                                rule->destination_cable);
            (void)snprintf(line, sizeof(line), "%u. source %s cable %s channel %s to %s cable %s",
                           index + 1u, source, cable, channel, destination, destination_cable);
        } else {
            (void)snprintf(line, sizeof(line), "%u. source %s cable %s channel %s %s",
                           index + 1u, source, cable, channel, action_name(rule->action));
        }
        cli_print(line);
    }
}

static void on_begin(EmbeddedCli *embedded_cli, char *args, void *context)
{
    (void)embedded_cli; (void)context;
    uint16_t count = embeddedCliGetTokenCount(args);
    const char *value = count == 0u ? "all" : embeddedCliGetToken(args, 1u);
    uint8_t action = ROUTE_ALL;
    if (count > 1u || (strcmp(value, "all") != 0 && strcmp(value, "drop") != 0)) {
        cli_print("Usage: begin [all|drop]"); return;
    }
    if (strcmp(value, "drop") == 0) action = ROUTE_DROP;
    cli_print(begin_policy(action) ? "Staging started; add rules, then save." :
                                     "Cannot begin while another transaction is active.");
}

static void on_rule(EmbeddedCli *embedded_cli, char *args, void *context)
{
    (void)embedded_cli; (void)context;
    uint16_t count = embeddedCliGetTokenCount(args);
    if (!transaction_active || (count != 4u && count != 6u)) {
        cli_print("Usage: rule SOURCE CABLE CHANNEL all|drop|to [DESTINATION CABLE]");
        cli_print("       rule DEVICE CABLE any exclude (exclude that port globally)");
        return;
    }
    route_rule_t rule = {0};
    const char *action_text = embeddedCliGetToken(args, 4u);
    bool valid = parse_device_token(embeddedCliGetToken(args, 1u), true, &rule.source_id) &&
                 parse_cable_token(embeddedCliGetToken(args, 2u), &rule.source_cable) &&
                 parse_channel_token(embeddedCliGetToken(args, 3u), &rule.channel);
    if (action_text != NULL && strcmp(action_text, "all") == 0 && count == 4u)
        rule.action = ROUTE_ALL;
    else if (action_text != NULL && strcmp(action_text, "drop") == 0 && count == 4u)
        rule.action = ROUTE_DROP;
    else if (action_text != NULL && strcmp(action_text, "exclude") == 0 && count == 4u) {
        rule.action = ROUTE_EXCLUDE;
        valid = valid && rule.source_id != 0u && rule.source_cable <= 15u &&
                rule.channel == ROUTE_ANY;
        rule.destination_id = 0u;
        rule.destination_cable = ROUTE_ANY;
    }
    else if (action_text != NULL && strcmp(action_text, "to") == 0 && count == 6u) {
        rule.action = ROUTE_TO;
        valid = valid && parse_device_token(embeddedCliGetToken(args, 5u), false,
                                            &rule.destination_id) &&
                parse_cable_token(embeddedCliGetToken(args, 6u), &rule.destination_cable);
    } else valid = false;
    if (valid) valid = stage_rule(&rule);
    else if (transaction_active) transaction_poisoned = true;
    cli_print(valid ? "Rule staged." : "Invalid rule; transaction is marked invalid.");
}

static void on_save(EmbeddedCli *embedded_cli, char *args, void *context)
{
    (void)embedded_cli; (void)args; (void)context;
    if (commit_policy()) cli_print("Configuration saved and active.");
    else {
        cli_print("Save failed (");
        cli_print(last_commit_error);
        cli_print("); staged configuration was discarded.");
    }
}

static void on_abort(EmbeddedCli *embedded_cli, char *args, void *context)
{
    (void)embedded_cli; (void)args; (void)context;
    abort_policy();
    cli_print("Staged configuration discarded.");
}

static void on_bootsel(EmbeddedCli *embedded_cli, char *args, void *context)
{
    (void)embedded_cli; (void)args; (void)context;
    cli_print("Entering BOOTSEL in 200 ms; press reset to cancel.");
    cdc_bench_request_bootsel();
}

static bool cli_initialize(void)
{
    if (cli != NULL) return true;
    EmbeddedCliConfig *config = embeddedCliDefaultConfig();
    config->rxBufferSize = 128u;
    config->cmdBufferSize = 96u;
    config->historyBufferSize = 192u;
    config->maxBindingCount = 8u;
    config->cliBuffer = cli_storage;
    config->cliBufferSize = sizeof(cli_storage);
    config->enableAutoComplete = false;
    cli = embeddedCliNew(config);
    if (cli == NULL) return false;
    cli_at_line_start = true;
    cli_pending_prompt = false;
    cli->writeChar = cli_write_char;
    static const CliCommandBinding bindings[] = {
        {"devices", "List known logical MIDI ports and cable counts.", true, NULL, on_devices},
        {"rules", "Show the saved default and ordered routing rules.", true, NULL, on_rules},
        {"show", "Alias for rules.", true, NULL, on_rules},
        {"begin", "begin [all|drop] starts a staged edit.", true, NULL, on_begin},
        {"rule", "rule SOURCE CABLE CHANNEL all|drop|to [DEST CABLE]; or DEVICE CABLE any exclude", true, NULL, on_rule},
        {"save", "Validate, persist and activate the staged rules.", true, NULL, on_save},
        {"abort", "Discard the staged edit.", true, NULL, on_abort},
        {"bootsel", "Reboot into the RP2040 USB bootloader.", true, NULL, on_bootsel},
    };
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
        if (!embeddedCliAddBinding(cli, bindings[i])) return false;
    }
    return true;
}

void route_control_init(bridge_t *bridge)
{
    active_bridge = bridge;
    route_control_reset_session();
}

void route_control_reset_session(void)
{
    transaction_active = false;
    transaction_poisoned = false;
    memset(&staged_policy, 0, sizeof(staged_policy));
    responses.head = 0;
    responses.count = 0;
    get_state = GET_IDLE;
    get_device_id = get_rule_index = 0;
    human_head = human_count = 0;
    welcome_pending = false;
    cli = NULL;
    cli_at_line_start = true;
    cli_pending_prompt = false;
    restart_notice_queued = false;
}

void route_control_start_session(void)
{
    welcome_pending = true;
}

void route_control_feed_human_line(const char *line, uint8_t length)
{
    if (line == NULL || !cli_initialize()) return;
    for (uint8_t i = 0; i < length; ++i) embeddedCliReceiveChar(cli, line[i]);
    embeddedCliReceiveChar(cli, '\r');
}

void route_control_feed_line(const char *line, uint8_t length)
{
    if (line == NULL || length < 2u || length >= RESPONSE_LINE_SIZE ||
        line[0] != 'R' || line[1] != ' ') return;
    char command[RESPONSE_LINE_SIZE];
    memcpy(command, line, length);
    command[length] = '\0';

    if (strcmp(command, "R GET") == 0) {
        if (get_state != GET_IDLE || transaction_active) response_error("busy");
        else { get_state = GET_CONFIG; get_device_id = 1; get_rule_index = 0; }
        return;
    }
    if (strncmp(command, "R BEGIN ", 8u) == 0) {
        uint8_t action;
        if (!parse_u8(command + 8u, &action) || !begin_policy(action)) response_error("begin");
        else (void)response_push("R OK");
        return;
    }
    if (strncmp(command, "R RULE ", 7u) == 0) {
        route_rule_t rule;
        if (!parse_rule(command, &rule) || !stage_rule(&rule)) {
            if (transaction_active) transaction_poisoned = true;
            response_error("rule");
        } else (void)response_push("R OK");
        return;
    }
    if (strcmp(command, "R COMMIT") == 0) {
        if (commit_policy()) (void)response_push("R OK");
        else response_error(last_commit_error);
        return;
    }
    if (strcmp(command, "R ABORT") == 0) {
        abort_policy();
        (void)response_push("R OK");
        return;
    }
    if (transaction_active) transaction_poisoned = true;
    response_error("command");
}

static bool make_get_line(char *line, size_t capacity)
{
    const route_policy_t *policy = route_store_policy();
    switch (get_state) {
    case GET_IDLE: return false;
    case GET_CONFIG:
        (void)snprintf(line, capacity, "R CONFIG %u", policy->default_action);
        get_state = GET_DEVICES; get_device_id = 1; return true;
    case GET_DEVICES:
        while (get_device_id <= ROUTE_STORE_MAX_ID) {
            uint8_t id = get_device_id++;
            const char *name = route_store_device_name(id);
            if (name == NULL) continue;
            uint8_t endpoint = 0xffu;
            for (uint8_t index = 0; index < USB_BRIDGE_ENDPOINT_COUNT; ++index) {
                if (active_bridge != NULL && active_bridge->router.endpoints[index].route_id == id) {
                    endpoint = index; break;
                }
            }
            uint8_t online = 0, rx = 0, tx = 0;
            if (endpoint != 0xffu && active_bridge->endpoints[endpoint].generation != 0u) {
                online = 1; rx = active_bridge->endpoints[endpoint].rx_cables;
                tx = active_bridge->endpoints[endpoint].tx_cables;
            }
            char name_hex[ROUTE_STORE_NAME_MAX_BYTES * 2u + 1u];
            static const char hex[] = "0123456789ABCDEF";
            size_t n = strlen(name);
            if (n > ROUTE_STORE_NAME_MAX_BYTES) n = ROUTE_STORE_NAME_MAX_BYTES;
            for (size_t i = 0; i < n; ++i) {
                unsigned char ch = (unsigned char)name[i];
                name_hex[2u * i] = hex[ch >> 4]; name_hex[2u * i + 1u] = hex[ch & 15u];
            }
            name_hex[2u * n] = '\0';
            (void)snprintf(line, capacity, "R DEVICE %u %u %u %u %s", id, online, rx, tx, name_hex);
            return true;
        }
        get_state = GET_RULES;
        /* fall through */
    case GET_RULES:
        if (get_rule_index < policy->count) {
            const route_rule_t *rule = &policy->rules[get_rule_index++];
            (void)snprintf(line, capacity, "R RULE %u %u %u %u %u %u", rule->source_id,
                           rule->source_cable, rule->channel, rule->action,
                           rule->destination_id, rule->destination_cable);
            return true;
        }
        get_state = GET_END;
        /* fall through */
    case GET_END:
        (void)snprintf(line, capacity, "R END"); get_state = GET_IDLE; return true;
    }
    return false;
}

static bool flush_human_output(void)
{
    if (human_count == 0u) return true;
    uint16_t contiguous = HUMAN_OUTPUT_CAPACITY - human_head;
    uint32_t length = human_count < contiguous ? human_count : contiguous;
    if (length > 64u) length = 64u;
    while (length > 0u) {
        if (usb_bridge_cdc_write(&human_output[human_head], length) == length) {
            human_head = (uint16_t)((human_head + length) % HUMAN_OUTPUT_CAPACITY);
            human_count = (uint16_t)(human_count - length);
            return human_count == 0u;
        }
        length /= 2u;
    }
    return false;
}

void route_control_task(void)
{
    if (route_store_restart_pending() && !restart_notice_queued) {
        restart_notice_queued = response_push("R INFO PIO host restarting after flash save");
    }
    if (welcome_pending) {
        static const char welcome[] = "rpico-bridge ready. Type help for commands.\r\n";
        for (size_t i = 0; i < sizeof(welcome) - 1u; ++i) (void)enqueue_human_char(welcome[i]);
        welcome_pending = false;
    }
    if (cli != NULL) embeddedCliProcess(cli);

    if (!flush_human_output()) return;
    if (responses.count != 0u && !cli_at_line_start) {
        (void)enqueue_human_char('\r');
        (void)enqueue_human_char('\n');
        cli_at_line_start = true;
        if (!flush_human_output()) return;
    }
    if (responses.count == 0u && get_state != GET_IDLE) {
        char generated[RESPONSE_LINE_SIZE];
        if (make_get_line(generated, sizeof(generated))) (void)response_push(generated);
    }
    if (responses.count == 0u) return;
    const char *line = responses.lines[responses.head];
    uint32_t length = (uint32_t)strlen(line);
    if (usb_bridge_cdc_write(line, length) == length) {
        responses.head = (uint8_t)((responses.head + 1u) % RESPONSE_QUEUE_DEPTH);
        --responses.count;
    }
}
