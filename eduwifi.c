#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

#define FIELD_SIZE 256

enum eap_method { METHOD_PEAP, METHOD_TTLS, METHOD_TLS };

// Everything needed to write one iwd .8021x profile
struct eap_config {
    char ssid[FIELD_SIZE];
    enum eap_method method;
    char identity[FIELD_SIZE];        // Outer identity, the first thing the server sees
    char username[FIELD_SIZE];        // Inner identity (PEAP/TTLS)
    char password[FIELD_SIZE];        // Inner password (PEAP/TTLS)
    const char *phase2_method;        // Inner method (PEAP/TTLS)
    char ca_cert[PATH_MAX];
    char server_domain[FIELD_SIZE];
    char client_cert[PATH_MAX];       // EAP-TLS only
    char client_key[PATH_MAX];        // EAP-TLS only
    char key_passphrase[FIELD_SIZE];  // EAP-TLS only
};

static int dry_run = 0;

// Function to enable raw mode for arrow key input
void enable_raw_mode() {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Function to restore normal terminal mode
void disable_raw_mode() {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag |= (ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Function to display an arrow-key menu and return the selected index
int select_option(const char *title, const char **options, int count) {
    int choice = 0;
    int key;

    enable_raw_mode();

    while (1) {
        system("clear");
        printf("\033[1mEduwifi\033[0m - eduroam setup for iwd\n\n");
        printf("%s\n\n", title);

        for (int i = 0; i < count; i++) {
            if (i == choice) {
                printf("  \033[1;36m> %s\033[0m\n", options[i]);  // Highlight selected option
            } else {
                printf("    %s\n", options[i]);
            }
        }
        printf("\n\033[2mUse the arrow keys, then press Enter\033[0m\n");
        fflush(stdout);

        key = getchar();
        if (key == EOF) {
            disable_raw_mode();
            exit(EXIT_FAILURE);
        }
        if (key == '\033') { // Arrow key detected
            getchar(); // Skip '['
            switch (getchar()) {
                case 'A':  // Up arrow
                    if (choice > 0) choice--;
                    break;
                case 'B':  // Down arrow
                    if (choice < count - 1) choice++;
                    break;
            }
        } else if (key == '\n' || key == '\r') {  // Enter key pressed
            break;
        }
    }

    disable_raw_mode();
    return choice;
}

// Function to read a line of input, falling back to a default when left empty
void read_line(const char *prompt, char *buf, size_t size, const char *fallback) {
    if (fallback != NULL && fallback[0] != '\0') {
        printf("%s [%s]: ", prompt, fallback);
    } else {
        printf("%s: ", prompt);
    }
    fflush(stdout);

    if (fgets(buf, size, stdin) == NULL) {
        printf("\n");
        exit(EXIT_FAILURE);
    }
    buf[strcspn(buf, "\r\n")] = 0;  // Remove newline character

    if (buf[0] == '\0' && fallback != NULL) {
        snprintf(buf, size, "%s", fallback);
    }
}

// Function to securely get a password without displaying it
void get_password(const char *prompt, char *password, size_t size) {
    struct termios oldt, newt;
    printf("%s: ", prompt);
    fflush(stdout);

    // Turn off terminal echo
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt);

    // Get user input
    if (fgets(password, size, stdin) == NULL) {
        password[0] = '\0';
    }
    password[strcspn(password, "\r\n")] = 0;  // Remove newline character

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
    printf("\n");  // Move to a new line
}

// eduroam routes every login by its realm (the part after "@"). Without one the
// home server is never reached and iwd fails right after the Identity exchange.
int has_realm(const char *identity) {
    const char *at = strchr(identity, '@');
    return at != NULL && at != identity && at[1] != '\0';
}

// Function to ask for an identity, warning when the realm is missing
void read_identity(const char *prompt, char *buf, size_t size, const char *fallback) {
    char answer[8];

    while (1) {
        read_line(prompt, buf, size, fallback);
        if (buf[0] == '\0') {
            continue;
        }
        if (has_realm(buf)) {
            return;
        }

        printf("  \033[1;33mWarning:\033[0m \"%s\" has no realm (e.g. you@university.edu).\n", buf);
        printf("  eduroam rejects identities without a realm with \"eapFail\".\n");
        read_line("  Use it anyway? [y/N]", answer, sizeof(answer), NULL);
        if (answer[0] == 'y' || answer[0] == 'Y') {
            return;
        }
    }
}

// Function to ask for a file path and turn it into an absolute path iwd can read
void read_file_path(const char *prompt, char path[PATH_MAX], int required) {
    char input[PATH_MAX];

    while (1) {
        read_line(prompt, input, sizeof(input), NULL);
        if (input[0] == '\0' && !required) {
            path[0] = '\0';
            return;
        }
        if (input[0] != '\0' && realpath(input, path) != NULL && access(path, R_OK) == 0) {
            return;
        }
        printf("  Can't read that file. Enter the full path, e.g. /home/you/Downloads/ca.pem\n");
    }
}

// Function to find the first wireless interface, falling back to wlan0
void detect_interface(char *interface, size_t size) {
    DIR *dir = opendir("/sys/class/net");
    struct dirent *entry;
    char path[PATH_MAX];
    struct stat st;

    snprintf(interface, size, "wlan0");
    if (dir == NULL) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", entry->d_name);
        if (stat(path, &st) == 0) {
            snprintf(interface, size, "%s", entry->d_name);
            break;
        }
    }
    closedir(dir);
}

// Interface names are passed to the shell, so only allow the characters Linux uses
int valid_interface(const char *interface) {
    if (interface[0] == '\0' || strlen(interface) > 15) {
        return 0;
    }
    for (const char *c = interface; *c; c++) {
        if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_' && *c != '.') {
            return 0;
        }
    }
    return 1;
}

// Function to build the iwd profile path. iwd stores SSIDs with characters other
// than letters, digits, space, '-' and '_' as "=<hex>.8021x"
void profile_path(const char *ssid, char *path, size_t size) {
    int plain = 1;
    size_t len;

    for (const char *c = ssid; *c; c++) {
        if (!isalnum((unsigned char)*c) && *c != ' ' && *c != '-' && *c != '_') {
            plain = 0;
            break;
        }
    }

    if (plain) {
        snprintf(path, size, "/var/lib/iwd/%s.8021x", ssid);
        return;
    }

    snprintf(path, size, "/var/lib/iwd/=");
    for (const char *c = ssid; *c; c++) {
        len = strlen(path);
        snprintf(path + len, size - len, "%02x", (unsigned char)*c);
    }
    len = strlen(path);
    snprintf(path + len, size - len, ".8021x");
}

// Function to write "Key=value", escaped the way iwd's settings parser expects
void write_setting(FILE *file, const char *key, const char *value) {
    if (value == NULL || value[0] == '\0') {
        return;
    }

    fprintf(file, "%s=", key);
    for (const char *c = value; *c; c++) {
        if (*c == '\\') {
            fputs("\\\\", file);
        } else if (c == value && *c == ' ') {
            fputs("\\s", file);  // A leading space would otherwise be trimmed
        } else {
            fputc(*c, file);
        }
    }
    fputc('\n', file);
}

// Same as write_setting, but never shows the secret in a dry run
void write_secret(FILE *file, const char *key, const char *value) {
    if (dry_run && value[0] != '\0') {
        fprintf(file, "%s=********\n", key);
    } else {
        write_setting(file, key, value);
    }
}

// Function to write the eduroam profile to /var/lib/iwd/
void write_eduroam_config(const struct eap_config *cfg) {
    const char *methods[] = { "PEAP", "TTLS", "TLS" };
    const char *method = methods[cfg->method];
    char filename[PATH_MAX];
    char key[64];
    FILE *file;

    profile_path(cfg->ssid, filename, sizeof(filename));

    if (dry_run) {
        printf("\n\033[1mDry run - this would be written to %s:\033[0m\n\n", filename);
        file = stdout;
    } else {
        mkdir("/var/lib/iwd", 0700);  // Already exists on most systems

        // The profile holds credentials, so only root may read it
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0 || fchmod(fd, 0600) != 0 || (file = fdopen(fd, "w")) == NULL) {
            perror(filename);
            exit(EXIT_FAILURE);
        }
    }

    fprintf(file, "[Security]\n");
    fprintf(file, "EAP-Method=%s\n", method);
    write_setting(file, "EAP-Identity", cfg->identity);

    snprintf(key, sizeof(key), "EAP-%s-CACert", method);
    write_setting(file, key, cfg->ca_cert);
    snprintf(key, sizeof(key), "EAP-%s-ServerDomainMask", method);
    write_setting(file, key, cfg->server_domain);

    if (cfg->method == METHOD_TLS) {
        if (cfg->client_key[0] != '\0') {
            write_setting(file, "EAP-TLS-ClientCert", cfg->client_cert);
            write_setting(file, "EAP-TLS-ClientKey", cfg->client_key);
        } else {
            // Certificate and key in one file (e.g. the .p12 from CAT)
            write_setting(file, "EAP-TLS-ClientKeyBundle", cfg->client_cert);
        }
        write_secret(file, "EAP-TLS-ClientKeyPassphrase", cfg->key_passphrase);
    } else {
        snprintf(key, sizeof(key), "EAP-%s-Phase2-Method", method);
        write_setting(file, key, cfg->phase2_method);
        snprintf(key, sizeof(key), "EAP-%s-Phase2-Identity", method);
        write_setting(file, key, cfg->username);
        snprintf(key, sizeof(key), "EAP-%s-Phase2-Password", method);
        write_secret(file, key, cfg->password);
    }

    fprintf(file, "\n[Settings]\n");
    fprintf(file, "Autoconnect=true\n");

    if (!dry_run) {
        if (fclose(file) != 0) {
            perror(filename);
            exit(EXIT_FAILURE);
        }
        printf("\nWi-Fi profile written to %s\n", filename);
    }
}

// Function to change MAC address
void change_mac_address(const char *interface) {
    char command[128];

    if (dry_run) {
        printf("\nDry run - would randomize the MAC address of %s\n", interface);
        return;
    }

    snprintf(command, sizeof(command), "ip link set dev %s down", interface);
    if (system(command) != 0) {
        fprintf(stderr, "Failed to bring %s down\n", interface);
        exit(EXIT_FAILURE);
    }

    snprintf(command, sizeof(command), "macchanger -r %s", interface);
    if (system(command) != 0) {
        fprintf(stderr, "Failed to change MAC address (is macchanger installed?)\n");
        exit(EXIT_FAILURE);
    }

    snprintf(command, sizeof(command), "ip link set dev %s up", interface);
    system(command);  // Bring interface up

    printf("MAC address changed successfully.\n");
}

void print_usage(const char *prog) {
    printf("Usage: sudo %s [-i interface] [-n]\n\n", prog);
    printf("  -i interface   Wireless interface (default: first one found, else wlan0)\n");
    printf("  -n             Dry run: show the profile instead of writing it (no root needed)\n");
    printf("  -h             Show this help\n");
}

void print_pikachu() {
    printf(" @                                                                                                     \n");
    printf("                    @@@@@                                                                                                  \n");
    printf("                     @@-*@..@                                                                @@@                            \n");
    printf("                      @@.@.....@                                                       @@@@@@#+@                            \n");
    printf("                       @**........@                                               -@.....#++-#@                             \n");
    printf("                        @@@.........@                                          @........@%%++@@                              \n");
    printf("                         @@............@                                    @...........%%:@@@                               \n");
    printf("                           @.............                                 @............@@@@                                \n");
    printf("                            @.............@                             ..............@@@              @.                   \n");
    printf("                              @.............        @@@-.:@@@@        @..............@@=          @@....@                   \n");
    printf("                               %%@............@ @...................@ ...............@         @.........@                   \n");
    printf("                                  @..............................................@        @.............                    \n");
    printf("                                    @.........................................@       @.................                    \n");
    printf("                                        @.................................@      @.....................@                    \n");
    printf("                                       *...................................@ @.........................                     \n");
    printf("                                       .....................................@.........................@                     \n");
    printf("                                      @.....................................@........................@                      \n");
    printf("                                      @............................@@@@..............................                       \n");
    printf("                                      @....@@ @@@.................@   @@............................@                      \n");
    printf("                                      @...@@   %%@@...............@@   @#..........................@                        \n");
    printf("                                      @....@@@@@@.................@@@@@@.....@...............@*                             \n");
    printf("                                      .......................................@.........@@                                   \n");
    printf("                                     .####@...............................@@@......@                                        \n");
    printf("                                     ##++=##@......%%....@.@:....@......###*:##........                                      \n");
    printf("                                     #=++++*#.........................##++++*#.........@                                    \n");
    printf("                                     @#=++++#@.......................@#++++++%%...........@                                 \n");
    printf("                                      @###=##.........................*#####@@..............                                \n");
    printf("                                        .@@@...............................      @.........@                                \n");
    printf("                                         @...............................@.     @........@                                 \n");
    printf("                                         ...@.........................@....@ @........@                                     \n");
    printf("                                        @......@@................@@................@                                        \n");
    printf("                                        .............@..####*....................+                                          \n");
    printf("                                       @.....................................@.#..#.@                                       \n");
    printf("                                      @..............:.........................####..@.                                     \n");
    printf("                                     @........................@.................                                            \n");
    printf("                                   @..................@..........................                                           \n");
    printf("                                  @..........................@...................@                                          \n");
    printf("                                 @.....................@...................@.....+                                          \n");
    printf("                            ...@ ......@...............@....@.....................                                          \n");
    printf("                           .@%%...@..........................@.............@....... @...                                     \n");
    printf("                            @@..............................@...................@...@.@@                                    \n");
    printf("                             @..@...........................@.....................@.@.@                                     \n");
    printf("                               *..++*.......................@...........@............@                                      \n");
    printf("                                @...:=*.....@..........*-=..@..........@........##*@                                        \n");
    printf("                                  @...:=+++..#.........@.#@@@.........+.....#=**#=                                         \n");
    printf("                                   @#:....####:.....@.@     @.......@###@+--=+@                                             \n");
    printf("                                      @@@      @@@ @           @@@       ..@                                               \n");
}

int main(int argc, char **argv) {
    struct eap_config cfg = {0};
    char interface[FIELD_SIZE] = "";
    int opt;

    while ((opt = getopt(argc, argv, "i:nh")) != -1) {
        switch (opt) {
            case 'i':
                snprintf(interface, sizeof(interface), "%s", optarg);
                break;
            case 'n':
                dry_run = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!dry_run && geteuid() != 0) {
        fprintf(stderr, "eduwifi needs root to write to /var/lib/iwd. Run: sudo %s\n", argv[0]);
        return 1;
    }

    if (interface[0] == '\0') {
        detect_interface(interface, sizeof(interface));
    }
    if (!valid_interface(interface)) {
        fprintf(stderr, "Invalid interface name: %s\n", interface);
        return 1;
    }

    // Display menus and get user choices
    const char *actions[] = {
        "Change MAC address, set credentials",
        "Set credentials only (no MAC change)"
    };
    int change_mac = select_option("What would you like to do?", actions, 2) == 0;

    const char *methods[] = {
        "PEAP + MSCHAPv2   username and password (most common)",
        "TTLS              username and password (PAP or MSCHAPv2 inside)",
        "TLS               personal certificate from cat.eduroam.org"
    };
    cfg.method = select_option("Which EAP method does your institution use?\n"
                               "\033[2m(Your profile on https://cat.eduroam.org lists it)\033[0m",
                               methods, 3);

    if (cfg.method == METHOD_PEAP) {
        cfg.phase2_method = "MSCHAPV2";
    } else if (cfg.method == METHOD_TTLS) {
        const char *inner[] = { "PAP", "MSCHAPv2" };
        int choice = select_option("Which inner (phase 2) method does TTLS use?", inner, 2);
        cfg.phase2_method = choice == 0 ? "Tunneled-PAP" : "Tunneled-MSCHAPv2";
    }

    printf("\n");
    while (1) {
        read_line("Network name (SSID)", cfg.ssid, sizeof(cfg.ssid), "eduroam");
        if (strlen(cfg.ssid) <= 32) {
            break;
        }
        printf("  An SSID is at most 32 characters.\n");
    }

    // Ask for credentials
    if (cfg.method == METHOD_TLS) {
        read_identity("Identity (e.g. you@university.edu)", cfg.identity, sizeof(cfg.identity), NULL);
        read_file_path("Client certificate, .p12 or .pem (full path)", cfg.client_cert, 1);
        read_file_path("Private key (full path, Enter if it is inside the certificate file)", cfg.client_key, 0);
        get_password("Certificate password (Enter if none)", cfg.key_passphrase, sizeof(cfg.key_passphrase));
    } else {
        read_identity("Username (e.g. you@university.edu)", cfg.username, sizeof(cfg.username), NULL);
        get_password("Password", cfg.password, sizeof(cfg.password));
        printf("\033[2mSome institutions want an anonymous outer identity such as anonymous@university.edu.\n"
               "Check your cat.eduroam.org profile; press Enter to reuse your username.\033[0m\n");
        read_identity("Outer identity", cfg.identity, sizeof(cfg.identity), cfg.username);
    }

    read_file_path("CA certificate (full path, Enter to skip)", cfg.ca_cert, 0);
    if (cfg.ca_cert[0] != '\0') {
        read_line("Server domain, e.g. radius.university.edu (Enter to skip)",
                  cfg.server_domain, sizeof(cfg.server_domain), NULL);
    }

    // If option 1 is selected, change MAC address
    if (change_mac) {
        change_mac_address(interface);
    }

    // Write Wi-Fi credentials to iwd config
    write_eduroam_config(&cfg);

    // Print connection instructions
    printf("\n\033[1mConnect to %s:\033[0m\n", cfg.ssid);
    printf("  iwctl station %s scan\n", interface);
    printf("  iwctl station %s connect \"%s\"\n", interface, cfg.ssid);
    printf("\nYour credentials are saved, so iwctl won't ask for them.\n");
    printf("If it fails, check the log with: \033[1mjournalctl -u iwd -f\033[0m\n\n");

    print_pikachu();

    return 0;
}
