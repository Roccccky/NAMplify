# Third-party notices

NAMplify is licensed under the AGPL-3.0 (see [LICENSE](LICENSE)). That license does not replace the
licenses of the source code NAMplify builds on. Keep the relevant copyright and license notices when
redistributing it.

## Source code NAMplify builds on

| Project | License | Used for |
| --- | --- | --- |
| [JUCE](https://github.com/juce-framework/JUCE) 9.0.1 | AGPL-3.0 (or commercial) | Audio, plugin formats (VST3, AU), window and web view |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | MIT | Running NAM captures |
| [Eigen](https://eigen.tuxfamily.org) 3.4.0 | MPL-2.0 | Linear algebra inside the NAM core |
| [nlohmann/json](https://github.com/nlohmann/json) 3.12.0 (part of NeuralAmpModelerCore) | MIT | Reading `.nam` files |
| VST3 SDK (part of JUCE) | MIT | The VST3 plugin |
| ASIO SDK (Steinberg), Windows build only | GPL-3.0 option | ASIO driver support; fetched at build time |
| Microsoft Edge WebView2 SDK, Windows build only | Microsoft WebView2 SDK license | The interface on Windows |

None of these are stored in this repository; `tools/fetch-third-party.sh` and the release workflow
fetch them at the versions NAMplify is tested with.

### Libraries bundled inside JUCE

JUCE ships these libraries in its modules. Which of them end up in a NAMplify binary depends on the
modules and platform; their notices are kept here in full either way.

| Library | License |
| --- | --- |
| zlib | Zlib |
| libpng | libpng-2.0 |
| jpeglib | IJG: this software is based in part on the work of the Independent JPEG Group |
| HarfBuzz | MIT-Modern-Variant |
| SheenBidi | Apache-2.0 ([text](licenses/Apache-2.0.txt)) |
| LunaSVG, PlutoVG | MIT |
| FLAC, libogg, libvorbis | BSD-3-Clause |
| Chromium cubic_bezier | BSD-3-Clause |

Their full license texts are in the JUCE sources under `modules/*/` next to each library.

### Bundled content

- The three cabinet IRs in `assets/bundled-irs/` are made for NAMplify and are licensed with it.
- The TONE3000 logo and wordmark belong to TONE3000 and are shown only to name the service.
  Tones, captures, pictures and names downloaded from TONE3000 belong to TONE3000 and their creators,
  under the license each tone states.
- Windows installers carry Microsoft's WebView2 runtime bootstrapper, which Microsoft allows to be
  redistributed.

## License texts

### NeuralAmpModelerCore (MIT License)

MIT License

Copyright (c) 2023 Steven Atkinson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### nlohmann/json (MIT License)

MIT License

Copyright (c) 2013-2025 Niels Lohmann


Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### VST3 SDK (MIT License)


Copyright (c) 2025, Steinberg Media Technologies GmbH

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


### Eigen (Mozilla Public License 2.0)

Eigen is used unmodified. Its source is available at <https://gitlab.com/libeigen/eigen>.
Full text: [licenses/MPL-2.0.txt](licenses/MPL-2.0.txt).

### JUCE (GNU Affero General Public License v3)

NAMplify uses JUCE under the AGPLv3. The full text is NAMplify's own [LICENSE](LICENSE).
