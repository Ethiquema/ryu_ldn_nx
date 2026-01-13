# Contributing to ryu_ldn_nx

Thank you for your interest in contributing to ryu_ldn_nx! This document provides guidelines for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Making Changes](#making-changes)
- [Commit Guidelines](#commit-guidelines)
- [Developer Certificate of Origin (DCO)](#developer-certificate-of-origin-dco)
- [Pull Request Process](#pull-request-process)
- [Coding Standards](#coding-standards)
- [Testing](#testing)

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

## Getting Started

1. Fork the repository on GitHub
2. Clone your fork locally
3. Set up the development environment (see below)
4. Create a branch for your changes
5. Make your changes
6. Submit a pull request

## Development Setup

### Using Docker (Recommended)

```bash
# Clone with submodules
git clone --recursive https://github.com/Ethiquema/ryu_ldn_nx.git
cd ryu_ldn_nx

# Build sysmodule
docker-compose run --rm build

# Run tests
docker-compose run --rm test

# Build overlay
docker-compose run --rm overlay
```

### Native Build

See the [README.md](README.md) for native setup instructions.

## Making Changes

1. Create a new branch from `main`:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. Make your changes, following the [Coding Standards](#coding-standards)

3. Test your changes thoroughly

4. Commit your changes with a descriptive message (see [Commit Guidelines](#commit-guidelines))

## Commit Guidelines

- Use clear, descriptive commit messages
- Start with a verb in the present tense (e.g., "Add", "Fix", "Update")
- Keep the first line under 72 characters
- Reference issues when applicable (e.g., "Fix #123")

Example:
```
Add exponential backoff for network reconnection

Implements retry logic with exponential backoff when the server
connection is lost. Maximum retry interval is 30 seconds.

Signed-off-by: Your Name <your.email@example.com>
```

## Developer Certificate of Origin (DCO)

This project uses the Developer Certificate of Origin (DCO) to ensure that contributors have the right to submit their code.

### What is the DCO?

The DCO is a lightweight way for contributors to certify that they wrote or otherwise have the right to submit the code they are contributing. The full text of the DCO is available at [developercertificate.org](https://developercertificate.org/).

### How to Sign Off

All commits must include a "Signed-off-by" line. This is done by adding the `-s` flag to your commit command:

```bash
git commit -s -m "Your commit message"
```

This will add a line like:
```
Signed-off-by: Your Name <your.email@example.com>
```

### Configuring Git

Make sure your Git configuration has your real name and email:

```bash
git config --global user.name "Your Real Name"
git config --global user.email "your.email@example.com"
```

### Fixing Unsigned Commits

If you forgot to sign off a commit, you can amend it:

```bash
git commit --amend -s
```

For multiple commits, you may need to rebase:

```bash
git rebase -i HEAD~N  # where N is the number of commits
# Mark commits as "reword" and add sign-off to each
```

## Pull Request Process

1. Ensure your code follows the [Coding Standards](#coding-standards)
2. Update documentation if needed
3. Add tests for new functionality
4. Make sure all tests pass
5. Ensure all commits are signed off (DCO)
6. Submit your pull request with a clear description
7. Address any review feedback

### PR Checklist

Before submitting, verify:

- [ ] Code compiles without warnings
- [ ] All tests pass
- [ ] Documentation is updated
- [ ] Commits are signed off (DCO)
- [ ] Branch is up to date with main

## Coding Standards

### C/C++ Style

- Use 4 spaces for indentation (no tabs)
- Opening braces on the same line
- Use Doxygen-style comments for documentation
- Follow the existing code style in the project
- Use meaningful variable and function names

Example:
```cpp
/**
 * @brief Brief description of the function
 * @param param1 Description of parameter
 * @return Description of return value
 */
Result FunctionName(Type param1) {
    if (condition) {
        // Code here
    }
    return result;
}
```

### Memory Management

- Minimize dynamic allocations on Switch
- Use static buffers where possible
- Follow stratosphere patterns for memory

### Error Handling

- Always check return values
- Use Result types for error propagation
- Log errors appropriately

## Testing

### Building

```bash
cd sysmodule
make
```

### Running Tests

```bash
docker-compose run --rm test
```

### Testing on Hardware

1. Build the project
2. Copy the output to your Switch SD card
3. Test the functionality
4. Report any issues

## Questions?

If you have questions about contributing, please open an issue on GitHub.

## License

By contributing to ryu_ldn_nx, you agree that your contributions will be licensed under the GNU General Public License v2.0.