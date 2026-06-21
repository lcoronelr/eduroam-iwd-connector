#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

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

// Function to display the menu and get user choice
int display_menu() {
    int choice = 0;
    char key;
    const char *options[] = {
        "Change MAC address, set credentials",
        "Set credentials only (no MAC change)"
    };

    enable_raw_mode();

    while (1) {
        system("clear");
        printf("Welcome to the Nurse Wi-Fi Setup Menu\n\n");

        for (int i = 0; i < 2; i++) {
            if (i == choice) {
                printf("> %s\n", options[i]);  // Highlight selected option
            } else {
                printf("  %s\n", options[i]);
            }
        }

        key = getchar();
        if (key == '\033') { // Arrow key detected
            getchar(); // Skip '['
            switch (getchar()) {
                case 'A':  // Up arrow
                    if (choice > 0) choice--;
                    break;
                case 'B':  // Down arrow
                    if (choice < 1) choice++;
                    break;
            }
        } else if (key == '\n') {  // Enter key pressed
            break;
        }
    }

    disable_raw_mode();
    return choice + 1;  // Return 1 or 2
}

// Function to securely get a password without displaying it
void get_password(char *password, size_t size) {
    struct termios oldt, newt;
    printf("Enter your eduroam password: ");

    // Turn off terminal echo
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt);

    // Get user input
    fgets(password, size, stdin);
    password[strcspn(password, "\n")] = 0;  // Remove newline character

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
    printf("\n");  // Move to a new line
}

// Function to write eduroam credentials to /var/lib/iwd/
void write_eduroam_config(const char *username, const char *password, const char *ssid) {
    char filename[256];
    snprintf(filename, sizeof(filename), "/var/lib/iwd/%s.8021x", ssid);
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }

    fprintf(file, "[Security]\n");
    fprintf(file, "EAP-Method=PEAP\n");
    fprintf(file, "EAP-Identity=%s\n", username);
    fprintf(file, "EAP-Password=%s\n", password);
    fprintf(file, "EAP-PEAP-Phase2-Method=MSCHAPV2\n\n");
    fprintf(file, "[Settings]\n");
    fprintf(file, "Autoconnect=true\n");

    fclose(file);
    printf("Wi-Fi credentials written successfully.\n");
    sleep(1);
}

// Function to change MAC address
void change_mac_address(const char *interface) {
    char command[128];

    snprintf(command, sizeof(command), "ip link set dev %s down", interface);
    system(command);  // Bring interface down

    snprintf(command, sizeof(command), "macchanger -r %s", interface);
    if (system(command) != 0) {
        perror("Failed to change MAC address (is macchanger installed?)");
        exit(EXIT_FAILURE);
    }

    snprintf(command, sizeof(command), "ip link set dev %s up", interface);
    system(command);  // Bring interface up

    printf("MAC address changed successfully.\n");
    sleep(1);
}

int main() {
    char ssid[128];
    char username[128];
    char password[128];
    const char *interface = "wlan0";  // Change this if your interface is different

    // Display menu and get user choice
    int choice = display_menu();

    printf("Enter your network name default (SSID) [default: eduroam]: ");
    scanf("%127s", ssid);
    getchar();  // Consume newline left by scanf

    // Ask for credentials
    printf("Enter your network username: ");
    scanf("%127s", username);
    getchar();  // Consume newline left by scanf

    get_password(password, sizeof(password));

    // If option 1 is selected, change MAC address
    if (choice == 1) {
        change_mac_address(interface);
    }

    // Write Wi-Fi credentials to iwd config
    write_eduroam_config(username, password, ssid);

    // Print step-by-step instructions
    printf("\nWi-Fi credentials saved successfully.\n");
    sleep(1);

    printf("\n\033[1mFollow these steps to connect to eduroam:\033[0m\n");
    sleep(1);

    printf("\n--> Open a terminal.\n");
    sleep(1);

    printf("--> Run: \033[1miwctl\033[0m\n");
    sleep(1);

    printf("--> Inside iwctl, type:\n");
    printf("    \033[1mstation wlan0 scan\033[0m\n");
    printf("    \033[1mstation wlan0 get-networks\033[0m\n");
    printf("    \033[1mstation wlan0 connect eduroam\033[0m\n");
    printf("    \033[1menter your full school email");
    printf("    \033[1menter your full password");
    printf("    \033[1myou are all set");
    
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

    
    return 0;
}

