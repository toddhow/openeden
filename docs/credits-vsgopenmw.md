# Credits: vsgopenmw

OpenEden's renderer is transplanted from **vsgopenmw** (https://github.com/vsgopenmw-dev/vsgopenmw), a rewrite of OpenMW on Vulkan and VulkanSceneGraph. The import was merged from vsgopenmw commit `2830e7e2b4` (decision 0002, task 0009, research notes 0002 and 0003). vsgopenmw is GPL-3.0, like OpenEden; transplanted files keep their copyright notices and the commit messages name the vsgopenmw commits they came from.

## Authors

These are the names vsgopenmw's own `vsgopenmw-authors.doc` listed, with the roles it gave them:

| Name | Role in vsgopenmw |
| --- | --- |
| Robert Osfield | VSG development (author of VulkanSceneGraph) |
| Julien Voisin (jvoisin/ap0), siystar | Public relations |
| DAGOTH_OHM | Lead design |
| Brian C. (MainBandit) | Lead development |

Credit for vsgopenmw's Android guide went to Grima04 and the XDA-Developers community (from its `vsgopenmw-build.md`).

## Notes kept from vsgopenmw's own files

- vsgopenmw describes itself as "Charityware" (its `vsgopenmw-code-of-conduct.doc`): it may be used and copied freely, and its authors encourage a donation to environmental causes (it names https://www.wwf.org). That is their request, not a licence term; the licence is the GPL-3.0 text in `LICENSE`.
- vsgopenmw's own project files (its readme, code style, roadmap, feature list, contributing notes and merge plan) were removed from the repository root in the cleanup of 2026-11-03 (research note 0030). They stay in git history, and in vsgopenmw's repository at the commit above.
- Its debug environment variables, which OpenEden's renderer still reads, are in `docs/debug-environment-variables.md`.
