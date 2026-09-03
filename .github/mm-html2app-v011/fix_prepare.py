from pathlib import Path
src = Path('.github/mm-html2app-v011/prepare_v011.py')
dst = Path('.github/mm-html2app-v011/prepare_v011_fixed.py')
s = src.read_text(encoding='utf-8')
old1 = "bootstrap = r'''def build_bootstrap_source"
new1 = 'bootstrap = r"""def build_bootstrap_source'
if old1 not in s:
    raise SystemExit('bootstrap opening delimiter not found')
s = s.replace(old1, new1, 1)
old2 = "}}\n'''\n'''\ns = re.sub(r'def build_bootstrap_source"
new2 = "}}\n'''\n\"\"\"\ns = re.sub(r'def build_bootstrap_source"
if old2 not in s:
    raise SystemExit('bootstrap closing delimiter not found')
s = s.replace(old2, new2, 1)
dst.write_text(s, encoding='utf-8')
print(dst)
