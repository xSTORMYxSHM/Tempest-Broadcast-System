# Tempest Vertical Canvas upstream

This directory is a namespaced, in-tree fork of Aitum Vertical Canvas 1.6.4.

- Upstream repository: https://github.com/Aitum/obs-vertical-canvas
- Upstream tag: `1.6.4`
- Upstream commit: `9cd13b8f3cd9afe01d665a2c869a24e88b9a8555`
- License: GPL-2.0; the upstream `LICENSE` file is retained in this directory.

Tempest-specific changes keep the component under the Broadcast release and
update lifecycle, namespace its module and integration identifiers, and disable
the standalone Aitum update check. The managed build also adopts existing Aitum
configuration, safely coordinates the OBS Additional Canvas selection, and
prevents portrait resolution resets while an output is active. Keep those
changes when refreshing from a future upstream tag.
