"""Regenerate the original installer artwork (optional; requires Pillow)."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ASSETS = Path(__file__).resolve().parent / "assets"
BLUE = "#246bdd"
INK = "#253140"


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(f"C:/Windows/Fonts/{'segoeuib' if bold else 'segoeui'}.ttf", size)


def label_mark(size: int) -> Image.Image:
    image = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(image)
    unit = size / 100
    def box(bounds: tuple[int, int, int, int]) -> tuple[float, ...]:
        return tuple(value * unit for value in bounds)
    draw.rounded_rectangle(box((1, 1, 99, 99)), radius=22 * unit, fill=BLUE)
    draw.rounded_rectangle(box((19, 22, 81, 78)), radius=6 * unit, fill="white")
    draw.line(box((30, 35, 61, 35)), fill=INK, width=max(1, round(4 * unit)))
    for x, width in ((30, 3), (36, 1), (40, 4), (47, 2), (52, 1), (56, 4), (63, 2), (68, 2)):
        draw.rectangle(box((x, 46, x + width, 63)), fill=INK)
    return image


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    icon = label_mark(1024).resize((256, 256), Image.Resampling.LANCZOS)
    icon.save(ASSETS / "setup.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    small = Image.new("RGB", (110, 110), "white")
    mark = label_mark(256).resize((86, 86), Image.Resampling.LANCZOS)
    small.paste(mark, (12, 12), mark)
    small.save(ASSETS / "wizard-small.bmp")

    large = Image.new("RGB", (328, 628), "#f1f3f5")
    draw = ImageDraw.Draw(large)
    for y in range(290, 628, 16):
        for x in range(24, 328, 16):
            draw.ellipse((x, y, x + 1, y + 1), fill="#ccd5df")
    mark = label_mark(512).resize((104, 104), Image.Resampling.LANCZOS)
    large.paste(mark, (30, 42), mark)
    draw.text((30, 178), "QtZpl", font=font(39, True), fill=INK)
    draw.text((30, 225), "Designer", font=font(29), fill=INK)
    draw.line((30, 289, 86, 289), fill=BLUE, width=4)
    draw.rounded_rectangle((29, 344, 297, 504), radius=10, fill="white", outline="#dce1e7", width=2)
    draw.text((49, 365), "LABEL / 001", font=font(20, True), fill=INK)
    draw.line((49, 404, 237, 404), fill="#dce1e7", width=2)
    for x, width in ((49, 5), (59, 2), (65, 7), (77, 4), (86, 2), (93, 5), (103, 2), (110, 7), (122, 3), (131, 6), (142, 2), (148, 4), (157, 7), (169, 2), (177, 5), (187, 3), (195, 7), (207, 2), (214, 5), (224, 3), (233, 5), (243, 3), (251, 5)):
        draw.rectangle((x, 424, x + width, 472), fill=INK)
    draw.text((30, 565), "ZPL  /  WINDOWS x64", font=font(16), fill="#6a7583")
    large.save(ASSETS / "wizard-large.bmp")


if __name__ == "__main__":
    main()
