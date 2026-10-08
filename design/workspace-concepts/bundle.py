"""Build the portable, offline HTML deliverable from the editable source files."""
from base64 import b64encode
from pathlib import Path

root = Path(__file__).resolve().parent
html = (root / "index.html").read_text(encoding="utf-8")
css = (root / "workspace.css").read_text(encoding="utf-8")
js = (root / "workspace.js").read_text(encoding="utf-8")
image = b64encode((root / "assets/editor-preview.png").read_bytes()).decode("ascii")
js = js.replace("assets/editor-preview.png", "data:image/png;base64," + image)
html = html.replace('<link rel="stylesheet" href="workspace.css">', '<style>\n' + css + '\n</style>')
html = html.replace('<script src="workspace.js"></script>', '<script>\n' + js + '\n</script>')
out = root / "workspace-designs.html"
out.write_text(html, encoding="utf-8")
print(f"Created {out} ({out.stat().st_size} bytes)")
