# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| latest  | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in ryu_ldn_nx, please report it responsibly.

### How to Report

1. **Do NOT open a public issue** for security vulnerabilities
2. Send an email to the maintainers with:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Any suggested fixes (optional)

### What to Expect

- Acknowledgment within 48 hours
- Regular updates on the progress
- Credit in the security advisory (if desired)

### Scope

This security policy covers:
- The ryu_ldn_nx sysmodule
- Network protocol implementation
- Configuration handling

### Out of Scope

- Vulnerabilities in Nintendo Switch firmware
- Issues in devkitPro toolchain
- Vulnerabilities in Atmosphere
- The ryu_ldn server (separate project)

## Security Considerations

### Network Security

ryu_ldn_nx communicates over the network. Users should:
- Only connect to trusted ryu_ldn servers
- Use the sysmodule on private networks when possible
- Keep the sysmodule updated

### Console Security

This sysmodule requires:
- Custom firmware (Atmosphere)
- Proper installation in `/atmosphere/contents/`

The sysmodule does not:
- Modify system files
- Store sensitive data
- Require elevated privileges beyond LDN access
