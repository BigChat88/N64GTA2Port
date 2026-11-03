# intro/

Source images for the "powered by libdragon" dragon logo shown at power-on
(see [src/n64/logo_n64.c](../../src/n64/logo_n64.c)). `dragon1.png`..`dragon4.png`
are the four line-art layers (head, body, tail, wordmark).

They come from [lambertjamesd/n64brew2025](https://github.com/lambertjamesd/n64brew2025)
(`assets/images/intro/`), which in turn sourced them from the N64brew-GameJam2024
repository (Copyright (c) 2024 N64brew, MIT licensed; see the license header in
`src/n64/logo_n64.c`).

The Makefile converts them to `rom:/intro/*.sprite` with `mksprite -f I8`: the
grayscale value is used as both intensity and alpha, so black areas are transparent
and the line art is tinted with the RDP prim color.
