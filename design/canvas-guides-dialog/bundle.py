"""Build the six offline canvas-and-guides dialog concepts from src/ templates.

Every output page is self-contained: CSS, icons, theme script and the QtZpl
label raster are inlined. Placeholders in src pages use ``{{name key=value}}``.
"""
from base64 import b64encode
from pathlib import Path
import re

root = Path(__file__).resolve().parent
src = root / "src"

# The sample label used across QtZpl Designer concepts: 600 x 320 dots at 203 dpi.
W, H, DPI = 600, 320, 203
MARGIN = 16
H_GUIDES = [40, 140, 160]
V_GUIDES = [44, 220, 300]
LABEL = "data:image/png;base64," + b64encode((root / "assets/label-preview.png").read_bytes()).decode("ascii")

VARIANTS = [
    dict(file="01-compact-form.html", title="Компактная форма", sub="Одна колонка, одна таблица",
         h2="Компактная форма — тот же диалог, только по порядку",
         desc="Все настройки в одной колонке, сгруппированы по смыслу. Две вкладки с направляющими заменены "
              "одной таблицей: ось видна в каждой строке, значение правится прямо в ней. Длинные пояснения ушли "
              "в подсказки, осталась одна строка о том, что ориентиры не печатаются.",
         tradeoff="Минимум изменений в коде: те же виджеты и тот же порядок применения. Превью нет — "
                  "положение направляющей видно только числом."),
    dict(file="02-live-preview.html", title="Живое превью", sub="Настройки и этикетка рядом",
         h2="Живое превью — результат виден до «Применить»",
         desc="Слева — короткая форма, справа — этикетка из последнего растра QtZpl с сеткой, рамкой отступа "
              "и направляющими. Направляющие выбираются на превью или в ряду чипов; у выбранной — точное поле "
              "и расстояние до края.",
         tradeoff="Самый наглядный вариант, но диалог широкий (~920 px) и дублирует жесты, которые уже есть "
                  "на холсте."),
    dict(file="03-sections.html", title="Разделы", sub="Навигация слева",
         h2="Разделы — каждая группа на своей странице",
         desc="Навигация как в системных настройках: вид, сетка и привязка, отступ, направляющие, подписи "
              "выделения. Единицы закреплены под навигацией и действуют на все разделы. Таблица направляющих "
              "получает фильтр по оси и расстояние до дальнего края.",
         tradeoff="Диалог не растёт с новыми настройками, но простые флажки спрятаны за переходами. Для "
                  "дюжины параметров это пока избыточно."),
    dict(file="04-guides-by-axis.html", title="Направляющие по осям", sub="Две колонки + разметка",
         h2="Направляющие по осям — главное содержимое диалога",
         desc="Общие настройки сжаты в две строки сверху. Горизонтальные и вертикальные направляющие — "
              "в двух колонках, у каждой своя мини-схема этикетки. Внизу — быстрая разметка: центр, линии по "
              "рамке отступа, колонки и строки одной операцией.",
         tradeoff="Решает типовые задачи вёрстки этикетки, но быстрая разметка — новая функциональность "
                  "с тестами на округление до dots и дубликаты."),
    dict(file="05-quick-panel.html", title="Быстрая панель", sub="Всплывает у «···», без «ОК»",
         h2="Быстрая панель — настройки применяются сразу",
         desc="Вместо модального окна — всплывающая панель у кнопки «···» над холстом. Каждый флажок и шаг "
              "виден на этикетке мгновенно. Точный список направляющих открывается отдельной командой "
              "«Направляющие…».",
         tradeoff="Быстрее всего для частых действий, но нет «Отмены»: откат только через Ctrl+Z, и изменения "
                  "одного открытия панели нужно склеивать в один шаг истории."),
    dict(file="06-property-grid.html", title="Таблица свойств", sub="Дерево «свойство — значение»",
         h2="Таблица свойств — плотно, как в Qt Designer",
         desc="Все параметры — строки сворачиваемого дерева, направляющие вложены по осям. Внизу — описание "
              "выбранного свойства с допустимым диапазоном вместо постоянных пояснений в окне.",
         tradeoff="Компактно и легко расширяется, но важное и редкое выглядят одинаково, а правка через "
                  "редактор в ячейке медленнее флажков."),
]


def num(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return text if text != "-0" else "0"


def mm(dots: float) -> str:
    return f"{dots * 25.4 / DPI:.2f}".replace(".", ",")


def parse_guides(spec: str):
    for token in filter(None, spec.split(",")):
        yield token[0], float(token[1:])


def guide_line(axis: str, pos: float, cls: str) -> str:
    if axis == "h":
        return f'<line class="{cls}" x1="0" y1="{num(pos)}" x2="{W}" y2="{num(pos)}"/>'
    return f'<line class="{cls}" x1="{num(pos)}" y1="0" x2="{num(pos)}" y2="{H}"/>'


def label_group(scale, *, grid=True, margin=True, axes="hv", sel="", new="", fade=False) -> str:
    sel_axis, sel_pos = (sel[0], float(sel[1:])) if sel else (None, None)
    faded = ' class="pv-fade"' if fade else ""
    out = [f'<g transform="scale({scale})">',
           f'<rect class="pv-paper" width="{W}" height="{H}" vector-effect="non-scaling-stroke"/>',
           f'<image href="{LABEL}" width="{W}" height="{H}"{faded}/>']
    if grid:
        out += [f'<line class="pv-grid" x1="{x}" y1="0" x2="{x}" y2="{H}"/>' for x in range(40, W, 40)]
        out += [f'<line class="pv-grid" x1="0" y1="{y}" x2="{W}" y2="{y}"/>' for y in range(40, H, 40)]
    if margin:
        out.append(f'<rect class="pv-margin" x="{MARGIN}" y="{MARGIN}" width="{W - 2 * MARGIN}" height="{H - 2 * MARGIN}"/>')
    for axis, values in (("h", H_GUIDES), ("v", V_GUIDES)):
        if axis not in axes:
            continue
        out += [guide_line(axis, v, "pv-guide") for v in values if not (axis == sel_axis and v == sel_pos)]
    out += [guide_line(axis, pos, "pv-guide new") for axis, pos in parse_guides(new)]
    if sel_axis:
        out.append(guide_line(sel_axis, sel_pos, "pv-guide sel"))
    out.append("</g>")
    return "".join(out)


def preview(scale="0.84", rulers="1", pad="18", sel="", bg="1", tag="1", grid="1", margin="1", label="Предпросмотр этикетки"):
    s, p = float(scale), float(pad)
    rw, rh = (32, 20) if rulers == "1" else (0, 0)
    ox, oy = rw + p, rh + p
    lw, lh = W * s, H * s
    tw, th = ox + lw + p, oy + lh + p
    out = [f'<svg class="pv" viewBox="0 0 {num(tw)} {num(th)}" width="{num(tw)}" height="{num(th)}" role="img" aria-label="{label}">']
    if bg == "1":
        out.append(f'<rect class="pv-bg" width="{num(tw)}" height="{num(th)}"/>')
    out.append(f'<rect class="pv-shadow" x="{num(ox + 1)}" y="{num(oy + 3)}" width="{num(lw)}" height="{num(lh)}"/>')
    out.append(f'<g transform="translate({num(ox)} {num(oy)})">{label_group(s, grid=grid == "1", margin=margin == "1", sel=sel)}</g>')
    if sel and tag == "1":
        axis, pos = sel[0], float(sel[1:])
        text = f'{"Y" if axis == "h" else "X"} {mm(pos)} мм'
        width = len(text) * 6.1 + 12
        if axis == "h":
            x, y = ox + lw - width - 6, oy + pos * s - 22
        else:
            x, y = ox + pos * s + 6, oy + 6
        out.append(f'<g class="pv-tag"><rect x="{num(x)}" y="{num(y)}" width="{num(width)}" height="17" rx="3"/>'
                   f'<text x="{num(x + 6)}" y="{num(y + 12.5)}">{text}</text></g>')
    if rw:
        per_mm = DPI / 25.4 * s
        out.append(f'<rect class="pv-ruler" x="{rw}" width="{num(tw - rw)}" height="{rh}"/>')
        out.append(f'<rect class="pv-ruler" y="{rh}" width="{rw}" height="{num(th - rh)}"/>')
        out.append(f'<rect class="pv-ruler" width="{rw}" height="{rh}"/>')
        out.append(f'<line class="pv-ruler-edge" x1="{rw}" y1="{rh - .5}" x2="{num(tw)}" y2="{rh - .5}"/>')
        out.append(f'<line class="pv-ruler-edge" x1="{rw - .5}" y1="{rh}" x2="{rw - .5}" y2="{num(th)}"/>')
        for axis, extent, origin in (("x", W, ox), ("y", H, oy)):
            end = origin + extent * s
            last = int(extent * 25.4 / DPI)
            for m in range(last + 1):
                if per_mm < 3 and m % 5:
                    continue
                at = origin + m * per_mm
                length = 8 if m % 10 == 0 else 5 if m % 5 == 0 else 3
                if axis == "x":
                    out.append(f'<line class="pv-tick" x1="{num(at + .5)}" y1="{rh}" x2="{num(at + .5)}" y2="{rh - length}"/>')
                    if m % 10 == 0 and end - at > 36:
                        out.append(f'<text class="pv-rtext" x="{num(at + 3)}" y="10">{m}</text>')
                else:
                    out.append(f'<line class="pv-tick" x1="{rw}" y1="{num(at + .5)}" x2="{rw - length}" y2="{num(at + .5)}"/>')
                    if m % 10 == 0 and end - at > 22:
                        out.append(f'<text class="pv-rtext" x="3" y="{num(at + 10)}">{m}</text>')
            if axis == "x":
                out.append(f'<line class="pv-tick" x1="{num(end - .5)}" y1="{rh}" x2="{num(end - .5)}" y2="2"/>')
                out.append(f'<text class="pv-rtext" x="{num(end - 3)}" y="10" text-anchor="end">{mm(W)}</text>')
            else:
                out.append(f'<line class="pv-tick" x1="{rw}" y1="{num(end - .5)}" x2="2" y2="{num(end - .5)}"/>')
                out.append(f'<text class="pv-rtext" x="3" y="{num(end - 3)}">{mm(H)}</text>')
        for v in V_GUIDES:
            x = ox + v * s
            out.append(f'<path class="pv-mark" d="M{num(x - 3.5)} {rh - 6}h7l-3.5 5.5z"/>')
        for v in H_GUIDES:
            y = oy + v * s
            out.append(f'<path class="pv-mark" d="M{rw - 6} {num(y - 3.5)}v7l5.5-3.5z"/>')
    out.append("</svg>")
    return "".join(out)


def mini(axis="h", scale="0.5", sel="", new="", grid="0", margin="1", label="Схема направляющих"):
    s = float(scale)
    w, h = W * s, H * s
    axes = "hv" if axis == "hv" else axis if axis in "hv" else ""
    return (f'<svg class="pv" viewBox="-1 -1 {num(w + 2)} {num(h + 4)}" width="{num(w + 2)}" height="{num(h + 4)}" role="img" aria-label="{label}">'
            f'<rect class="pv-shadow" x="0" y="2" width="{num(w)}" height="{num(h)}"/>'
            + label_group(s, grid=grid == "1", margin=margin == "1", axes=axes, sel=sel, new=new, fade=True) + "</svg>")


ICONS = {
    "guide-h": '<rect x="2.5" y="2.5" width="11" height="11" rx="1" opacity=".45"/><path d="M.8 8h14.4" stroke-dasharray="2.2 1.6"/>',
    "guide-v": '<rect x="2.5" y="2.5" width="11" height="11" rx="1" opacity=".45"/><path d="M8 .8v14.4" stroke-dasharray="2.2 1.6"/>',
    "plus": '<path d="M8 3.5v9M3.5 8h9"/>',
    "x": '<path d="M4.5 4.5l7 7M11.5 4.5l-7 7"/>',
    "trash": '<path d="M3 4.5h10M6.5 4.5V3h3v1.5M4.6 4.5l.6 8.5h5.6l.6-8.5"/>',
    "grid": '<rect x="2.5" y="2.5" width="11" height="11" rx="1"/><path d="M6.2 2.5v11M9.8 2.5v11M2.5 6.2h11M2.5 9.8h11"/>',
    "ruler": '<rect x="1.5" y="5" width="13" height="6" rx="1"/><path d="M4.2 5v2.4M6.8 5v1.5M9.4 5v2.4M12 5v1.5"/>',
    "magnet": '<path d="M3.5 2.5h3v5a1.5 1.5 0 0 0 3 0v-5h3v5a4.5 4.5 0 0 1-9 0z"/><path d="M3.5 5h3M9.5 5h3"/>',
    "margin": '<rect x="2" y="2" width="12" height="12" rx="1"/><rect x="4.6" y="4.6" width="6.8" height="6.8" stroke-dasharray="1.6 1.4"/>',
    "eye": '<path d="M1.5 8s2.4-4.5 6.5-4.5S14.5 8 14.5 8s-2.4 4.5-6.5 4.5S1.5 8 1.5 8z"/><circle cx="8" cy="8" r="2"/>',
    "info": '<circle cx="8" cy="8" r="6.3"/><path d="M8 7.3v4M8 4.9v.1"/>',
    "reset": '<path d="M2.8 3v3.4h3.4"/><path d="M3.3 6.2A5 5 0 1 1 3.1 9.6"/>',
    "center": '<path d="M8 1v14M1 8h14" stroke-dasharray="2 1.6"/><circle cx="8" cy="8" r="2.2"/>',
    "columns": '<rect x="1.8" y="2.5" width="12.4" height="11" rx="1"/><path d="M6 2.5v11M10 2.5v11"/>',
    "dims": '<rect x="3.5" y="2.5" width="9" height="6" rx="1"/><path d="M2 12.5h12M2 11v3M14 11v3"/>',
    "more": '<circle cx="3.5" cy="8" r=".9" fill="currentColor"/><circle cx="8" cy="8" r=".9" fill="currentColor"/><circle cx="12.5" cy="8" r=".9" fill="currentColor"/>',
    "chev-down": '<path d="M4.5 6.5L8 10l3.5-3.5"/>',
    "chev-right": '<path d="M6.5 4.5L10 8l-3.5 3.5"/>',
    "search": '<circle cx="7" cy="7" r="4.3"/><path d="M10.2 10.2l3.6 3.6"/>',
    "label": '<rect x="1.5" y="3.5" width="13" height="9" rx="1"/><path d="M4 6.5h5M4 9.5h3"/>',
}
SPRITE = ('<svg class="sprite" aria-hidden="true">'
          + "".join(f'<symbol id="i-{name}" viewBox="0 0 16 16">{body}</symbol>' for name, body in ICONS.items())
          + "</svg>")


def icon(name):
    return f'<svg class="ic" aria-hidden="true"><use href="#i-{name}"/></svg>'


HELPERS = {"preview": preview, "mini": mini, "ic": icon, "mm": mm}


def expand(text: str) -> str:
    def replace(match):
        name, *args = match.group(1).split()
        positional = [a for a in args if "=" not in a]
        keywords = dict(a.split("=", 1) for a in args if "=" in a)
        if "label" in keywords:
            keywords["label"] = keywords["label"].replace("_", " ")
        if name == "mm":
            return mm(float(positional[0]))
        return HELPERS[name](*positional, **keywords)
    return re.sub(r"\{\{([^}]+)\}\}", replace, text)


THEME_BOOT = ("(function(){var t;try{t=localStorage.getItem('qtzpl-concept-theme')}catch(e){}"
              "if(t!=='dark'&&t!=='light')t=matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light';"
              "document.documentElement.dataset.theme=t})();")
THEME_TOGGLE = ("(function(){var r=document.documentElement,b=document.getElementById('theme-toggle');"
                "function sync(){b.textContent=r.dataset.theme==='dark'?'Светлая тема':'Тёмная тема'}sync();"
                "b.addEventListener('click',function(){r.dataset.theme=r.dataset.theme==='dark'?'light':'dark';"
                "try{localStorage.setItem('qtzpl-concept-theme',r.dataset.theme)}catch(e){}sync()})})();")

PAGE = """<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<script>{boot}</script>
<style>
{css}
</style>
</head>
<body>
{sprite}
<div class="page">
  <header class="page-head">
    <div><div class="eyebrow">QtZpl Designer / концепции диалога</div><h1>Холст и направляющие</h1></div>
    <div class="head-actions"><a class="gallery-button" href="index.html">Сравнение вариантов</a><button id="theme-toggle" class="gallery-button" type="button">Тёмная тема</button></div>
  </header>
  <nav class="variants" aria-label="Варианты диалога">{nav}</nav>
{caption}
{body}
  <footer class="foot"><span>Статический макет · этикетка 600 × 320 dots, 203 dpi, изображение получено через QtZpl · все значения хранятся в dots</span><nav>{pager}</nav></footer>
</div>
<script>{toggle}</script>
</body>
</html>
"""


def build():
    base_css = (src / "base.css").read_text(encoding="utf-8")
    pages = [("index.html", None)] + [(v["file"], v) for v in VARIANTS]
    for index, (name, meta) in enumerate(pages):
        body = (src / name).read_text(encoding="utf-8")
        extra_css = "\n".join(re.findall(r"<style>(.*?)</style>", body, flags=re.S))
        body = expand(re.sub(r"<style>.*?</style>\s*", "", body, flags=re.S))
        current = ' aria-current="page"'
        nav = "".join(
            f'<a class="variant" href="{v["file"]}"{current if v is meta else ""}>'
            f'<span class="vn">{i:02d}</span><div><strong>{v["title"]}</strong><small>{v["sub"]}</small></div></a>'
            for i, v in enumerate(VARIANTS, 1))
        if meta:
            caption = (f'  <section class="caption"><div><h2>{meta["h2"]}</h2><p>{meta["desc"]}</p></div>'
                       f'<p class="tradeoff">{meta["tradeoff"]}</p></section>')
            title = f'Холст · {meta["title"]}'
        else:
            caption = ""
            title = "Холст и направляющие"
        prev_page = pages[index - 1][0] if index > 0 else None
        next_page = pages[index + 1][0] if index + 1 < len(pages) else None
        pager = "".join(filter(None, [
            f'<a href="{prev_page}">← Назад</a>' if prev_page else "",
            f'<a href="{next_page}">Дальше →</a>' if next_page else ""]))
        html = PAGE.format(title=title, boot=THEME_BOOT, css=base_css + extra_css, sprite=SPRITE, nav=nav,
                           caption=caption, body=body, pager=pager, toggle=THEME_TOGGLE)
        (root / name).write_text(html, encoding="utf-8")
        print(f"{name}: {len(html.encode('utf-8'))} bytes")


if __name__ == "__main__":
    build()
