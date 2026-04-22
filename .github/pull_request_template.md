## Summary

Describe the change and why it is needed.

## Related Issues

Link issue(s), if applicable:

- Fixes #
- Related to #

## Testing

Describe what you ran locally:

- [ ] `cmake -S . -B build-dev -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug`
- [ ] `cmake --build build-dev --target appSecondBrain ellaMemoryBrowserTests --config Debug -j 4`
- [ ] `ctest --test-dir build-dev --output-on-failure -C Debug`

## Checklist

- [ ] PR scope is focused and reviewable
- [ ] Documentation updated (if behavior/setup changed)
- [ ] No generated artifacts or large vendor bundles added
- [ ] `Windows CI` passes
