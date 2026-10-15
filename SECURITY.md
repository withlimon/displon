# Security Policy

## Supported Versions

Displon ships daily releases (`0.1.YYYYMMDD`). Only the **latest release** receives security fixes.

| Version                | Supported          |
| ---------------------- | ------------------ |
| Latest release         | :white_check_mark: |
| Older releases         | :x:                |

Please update to the latest release before reporting an issue.

## Reporting a Vulnerability

**Do not open a public issue for security problems.**

Report privately via GitHub: go to the repository's **Security** tab → **Report a vulnerability**
(https://github.com/withlimon/displon/security/advisories/new).

Please include:
- A clear description and the potential impact
- Steps to reproduce (and a proof of concept if possible)
- Affected version and Windows version

### What to expect

- **Acknowledgement:** within 7 days.
- **Updates:** at least every 14 days until resolved.
- **Accepted:** a fix is prepared privately, released in the next daily release, and you are credited in the advisory (unless you prefer not to be).
- **Declined:** you get a short explanation of why it is not considered a vulnerability.

Please allow reasonable time to fix the issue before any public disclosure.

## Scope note

Displon installs a background service on Windows and needs admin rights to install. Issues involving privilege escalation through the service or installer are especially welcome.
