# Fonts and screenshots

UI text on the Vulkan device can be a multi-channel distance field. Screenshots can be JPEG, TGA, or JPEG XL.

## Distance-field fonts

`gxMsdfFonts` is the switch. It defaults to **off**.

```text
/console gxMsdfFonts 1
```

The release play script writes `SET gxMsdfFonts "1"` into `WTF/Config.wtf` before it starts `WowClient64.release`. A dev launch leaves the CVar alone. See [Platforms](platforms.md).

When the CVar is on, every font that is not monochrome is rebuilt as an RGB distance field and sampled in `Ffp.frag`. The outline of a glyph stays smooth when the quad is scaled, which is the point of the mode: the stock atlas is a small alpha mask and it softens as soon as the frame scales it. A font that sets the `MSDF` flag in XML, or `msdf="true"` on the font node, keeps that flag even if the CVar is later turned off.

The sampler runs on the Vulkan device only. On D3D9 and OpenGL the same flag is ignored and the stock atlas is used.

`MSDFMode`, the AwesomeWotLK CVar, only stores a number. It does not rebuild fonts. Addons that write `MSDFMode` are talking to this client; the pixels follow `gxMsdfFonts`.

The field is generated in `Engine/Source/Gxu/Msdf.cpp` with a pixel range of 4. The atlas for a distance-field face is RGBA8. The fragment shader reads the median of the three channels, turns that into a screen-pixel distance, and draws a body plus a 0, 1, or 2 pixel outline from the font's existing outline flags.

## Screenshots

| CVar | Default | Values |
|---|---|---|
| `screenshotFormat` | `jpeg` | `jpeg`, `tga`, `jxl` |
| `screenshotQuality` | `3` | 1–10 |

Print Screen on a wide window used to overflow the JPEG encoder's row buffer (the old cap matched a 16,384-byte line from the 32-bit client). The encoder now sizes that buffer from the capture width.

`jxl` is present when CMake found `libjxl` through pkg-config while building the `Images` library. Without that library the JXL writer returns failure and JPEG and TGA still work. The JXL path packs the capture as RGB before handing it to libjxl.
