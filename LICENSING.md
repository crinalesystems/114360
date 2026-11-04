# Licensing

Licenses follow folders.

| Path | License |
|---|---|
| `launcher/` | GNU Affero General Public License 3.0 (`launcher/LICENSE`). Its PlayOnline code (`launcher/src/pol/`) is reworked from Project Crystal Server (https://github.com/Project-Crystal-Server/Project-Crystal-Server), which is AGPL-3.0. |
| `launcher/third_party/microui/` | MIT (its `LICENSE`) |
| `installer/` | not chosen yet: all rights reserved by Miguel Dias De Freitas until a license is added here |
| `installer/third_party/presdl/` | BSD 3-Clause (its `LICENSE`) |
| `installer/third_party/monocypher/` | BSD 2-Clause or CC0 1.0 (its `LICENSE`) |
| `common/` | not chosen yet, as `installer/` |

The installer is a separate program from the launcher: it carries the built launcher in its zip
(`payload/default.xex`) but contains none of the launcher's code. `114360-installer -credits` lists every
component and prints each license in full. The license texts it prints are in `licenses/`.
