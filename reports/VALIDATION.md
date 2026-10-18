# Local validation

Target: Linux/glibc, x86-64, GCC 14.2.0; Clang was also available in the build environment.

- **15 distinct test cases passed with GCC.**
- The same 15 cases passed after a clean Clang build.
- The same 15 cases passed with UndefinedBehaviorSanitizer and non-recovering errors enabled.
- The two-thread FIFO example and yielding benchmark compiled and ran.
- Guard-page tests deliberately generated SIGSEGV in child processes and verified the exit signal.
- No Docker build or GitHub-hosted workflow run was performed here.

The attached benchmark output is one local measurement under unknown shared-host load. It includes
scheduler overhead and is not a standalone hardware context-switch cost. Do not present it as a
production latency guarantee. Passing tests and UBSan are not a formal proof of memory safety.
