# 0.7.5 distro compatibility: measurements

Every entry is a command that was run and what it printed, not an expectation.
Tasks append here; the final gate (Task 21) checks the record is complete.

## Task 1: baselines (before any porting)

| Target | Qt | Result |
|---|---|---|
| host (Arch) | 6.11.2 | gate H green: no build warnings; ctest `100% tests passed out of 74`; qmllint `baseline matches (1336 warnings)`; self-test 15/15, `selftest.sh: OK` |
| debian-13 | 6.8.2 | build clean; ctest `99% tests passed, 1 tests failed out of 74`, `tst_navigation_history` fails; self-test 15/15, `selftest.sh: OK` |
| ubuntu-24.04 | 6.4.2 | configure fails: `Could not find a configuration file for package "Qt6" that is compatible with requested version "6.8"` (`Qt6Config.cmake, version: 6.4.2`) |
