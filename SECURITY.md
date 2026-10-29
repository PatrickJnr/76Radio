# Security policy

## Supported versions

0.1.0 receives security fixes. Older builds are not supported; rebuild from the current source.

## Reporting a vulnerability

Use GitHub private vulnerability reporting: open the Security tab of the repository, then Report a vulnerability. Do not open a public issue for a vulnerability, and do not post exploit details in a discussion.

Include:

- The 76Radio version and, if possible, the SHA-256 of `version.dll`.
- The Windows version and the Fallout 76 version.
- What the issue allows an attacker to do.
- Steps to reproduce, using the smallest configuration that shows the problem.
- Any suggested fix.

Remove your Windows user name, personal paths and account details from logs. Do not include third-party secrets.

Acknowledgement is attempted within seven days. This is a volunteer project, so no response time is guaranteed and there is no bug bounty.

## Scope

In scope:

- The `version.dll` forwarding, exported functions and module lifetime.
- The document channel and parsing of commands, state and station data.
- Stream URL handling and ICY metadata parsing.
- ActionScript command and state handling in the Pip-Boy and HUD movies.
- Build and packaging scripts that process input from the repository or the game installation.

Out of scope:

- Fallout 76, xScal, HUDModLoader and the Spotify client themselves.
- Problems that require administrator access, a modified game binary or a man-in-the-middle position.
- Antivirus detections that cannot be reproduced against the published source.
- Gameplay, account or moderation issues.

## Disclosure

Please allow time for a fix and a release before publishing details. Credit is given unless you ask to remain anonymous.
