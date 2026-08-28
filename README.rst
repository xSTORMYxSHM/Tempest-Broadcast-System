Tempest Broadcast System
========================

Tempest Broadcast System is an independent, open-source Windows broadcast
workstation built from OBS Studio. It retains OBS's scene, source, plugin,
encoder, recording, and streaming engine while adding the Tempest Broadcast
production interface, reactive overlay design, asset management, audio-driven
effects, automation, and an optional connection to Tempest Studio.

The current Tempest product version is **0.16.0**. The upstream OBS engine
version remains visible separately in the application title and logs so plugin
and encoder compatibility can be diagnosed accurately.

Public preview
--------------

The first supported public target is 64-bit Windows 10 or Windows 11. Public
packages must include both the Windows binary archive and the complete source
archive from the exact same tagged commit. Installation, network behavior,
known limitations, privacy boundaries, checksums, and the publisher checklist
are documented in ``PUBLIC_RELEASE.md``.

Major Tempest surfaces
----------------------

- Tempest Broadcast control bar and responsive Command/Engineering workspaces
- Scene Control and Source Operations
- application-wide UI scaling from 60% through 160% and persistent color palettes
- Stream Overlay, file-backed content profiles, and modular reactive browser elements
- Asset Library, Overlay Designer, Audio Reactor, Sequence Director, and Media Controls
- optional authenticated localhost integration with Tempest Studio
- independent configuration storage alongside a normal OBS Studio installation

Source layout and development
-----------------------------

Fork-specific architecture, build commands, integrations, and feature details
are maintained in ``TEMPEST_FORK.md``. Warudo and Studio event routing is
documented in ``TEMPEST_WARUDO_BRIDGE.md``.

Create a clean tagged Windows release with::

  powershell -ExecutionPolicy Bypass -File scripts/Build-PublicRelease.ps1

The release script refuses dirty or untagged source, packages all pinned Git
submodules, generates SHA-256 checksums, and places artifacts on drive G when
available or in the local ``release`` directory otherwise.

OBS Studio foundation
---------------------

This project is based on `OBS Studio <https://github.com/obsproject/obs-studio>`_,
which is designed for capturing, compositing, encoding, recording, and streaming
video content. Tempest Broadcast System is not affiliated with or endorsed by
the OBS Project. Upstream documentation is available at
`obsproject.com <https://obsproject.com>`_.

License
-------

Tempest Broadcast System and OBS Studio are distributed under the GNU General
Public License version 2 or, at your option, any later version. See ``COPYING``,
``NOTICE.txt``, ``AUTHORS``, and the dependency-specific license files in the
source tree. The software is provided without warranty.
