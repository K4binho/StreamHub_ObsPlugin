# Compact YouTube and account UI

## Scope
- Remove visible Kick/YouTube **Avançado** buttons while keeping `.env` credential support and backend configuration routes intact.
- Put account action buttons on one horizontal row to reduce card height.
- Reduce account-card margins, icon size, and spacing.
- Replace full OAuth authorization URLs in status labels with short messages because browser opens URL automatically.
- Mark OAuth start/poll failures as disconnected and refresh label styling so errors do not appear green.

## Files
- `src/streamhub-control-dock.cpp`
- `src/streamhub-control-dock.h`

## Validation
- Run `git diff --check`.
- Run CMake configure/build.
- Run `node --check` only if Node files change; no Node files planned.
