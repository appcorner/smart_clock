# Smart Clock Configuration

This file contains local device settings for the SmartClock skill. 

**Important**: Copy this template to `.claude/settings.local.json` and update with your actual device settings.

## Setup Instructions

1. Copy this file:
   ```bash
   cp .claude/settings.json .claude/settings.local.json
   ```

2. Edit `.claude/settings.local.json` with your device settings:
   ```json
   {
     "smartclock": {
       "ip": "192.168.1.100",      // Your device IP address
       "user": "admin",             // HTTP Basic Auth username
       "pass": "yourpassword"       // HTTP Basic Auth password
     }
   }
   ```

3. The `.claude/settings.local.json` file is already in `.gitignore` so your credentials won't be committed to git.

## Finding Your Device IP

- Check your router's DHCP client list
- Use the device's web UI (if accessible via AP mode)
- Check serial monitor output during boot
- Use network scanning tools: `nmap -sn 192.168.1.0/24`

## Default Credentials

The default credentials from the firmware are:
- Username: `admin`
- Password: (check your firmware config or web UI)

You can change these via the device's `/config` endpoint.
