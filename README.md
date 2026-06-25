# Eduwifi - Secure Wi-Fi Credential Manager

Eduwifi is a lightweight, terminal-based utility for configuring **eduroam credentials** with `iwd`. It also includes an option to randomize your **MAC address** for enhanced privacy. With a simple menu-driven interface, Eduwifi makes it easy to securely store credentials and manually connect to eduroam networks.

## Features

- **Secure Wi-Fi Configuration:** Easily stores and manages eduroam credentials for `iwd`.  
- **MAC Address Randomization:** Optionally changes the MAC address before connecting for increased privacy.  
- **Interactive Menu:** Navigate options using the **arrow keys** for an intuitive experience.  
- **Step-by-Step Connection Guide:** Provides clear instructions for manually connecting via `iwctl`.  
## Preequesite
**for arch linux** 

```bash
sudo pacman -S macchanger
```

## Installation

***To install Eduwifi manually, run the following commands:***

```bash

git clone https://github.com/Nytso2/eduwifi.git
```
```bash

cd eduwifi
```
```bash
make
```
```bash

sudo make install
```
## Usage

After installation, you can run Eduwifi using:
```bash

sudo eduwifi
```
### Menu Options:

- **Change MAC address & set credentials**  
- **Set credentials only (no MAC change)**  

Use the **arrow keys** to navigate and **press Enter** to select an option.

## Entering Credentials
- Enter your **network ssid** (e.g., `eduroam-5Ghz`).  
- Enter your **eduroam username** (e.g., `yourname@university.edu`).  
- Enter your **Wi-Fi password** (input is hidden for security).  
- Eduwifi securely saves your credentials in `/var/lib/iwd/{SSID}.8021x`.  

## Manually Connecting to Eduroam

After saving credentials, you must manually connect using `iwctl`:

## iwctl

Inside `iwctl`, run:
```bash

station wlan0 scan
```
```bash
station wlan0 get-networks
```
```bash
station wlan0 connect eduroam
```
Then enter your **full school email** and **password** when prompted.

## Uninstallation

To remove the installed binary:
```bash
make clean
```
## Troubleshooting

If you experience **permission issues**, ensure:

- You are running Eduwifi with `sudo`.  
- `iwd` is installed and running:
```bash
  sudo systemctl start iwd
```
- To enable `iwd` to start on boot, run:
```bash
  sudo systemctl enable iwd
```
## Contributing

Pull requests and feature suggestions are welcome! Open an **issue** if you encounter bugs or have ideas for improvements.
