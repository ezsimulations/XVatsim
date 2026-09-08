from __future__ import annotations

import argparse
import html
import re
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import (
    Image,
    KeepTogether,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


VERSION = "2.0.0"
ACCENT = colors.HexColor("#17758A")
TEXT = colors.HexColor("#263640")
MUTED = colors.HexColor("#66757D")
RULE = colors.HexColor("#D8E0E4")
CODE_BG = colors.HexColor("#F4F6F7")


def inline_markup(value: str) -> str:
    parts = re.split(r"(`[^`]+`)", value)
    rendered: list[str] = []
    for part in parts:
        if len(part) >= 2 and part.startswith("`") and part.endswith("`"):
            rendered.append(
                f'<font name="Courier" color="#52616A">{html.escape(part[1:-1])}</font>'
            )
        else:
            rendered.append(html.escape(part))
    return "".join(rendered)


def build_styles():
    styles = getSampleStyleSheet()
    styles.add(
        ParagraphStyle(
            name="GuideTitle",
            parent=styles["Title"],
            fontName="Helvetica-Bold",
            fontSize=24,
            leading=28,
            textColor=TEXT,
            spaceAfter=7,
            alignment=0,
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideH2",
            parent=styles["Heading2"],
            fontName="Helvetica-Bold",
            fontSize=16,
            leading=19,
            textColor=TEXT,
            spaceBefore=10,
            spaceAfter=6,
            keepWithNext=True,
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideH3",
            parent=styles["Heading3"],
            fontName="Helvetica-Bold",
            fontSize=12,
            leading=15,
            textColor=ACCENT,
            spaceBefore=7,
            spaceAfter=4,
            keepWithNext=True,
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideBody",
            parent=styles["BodyText"],
            fontName="Helvetica",
            fontSize=9.4,
            leading=12.4,
            textColor=TEXT,
            spaceAfter=6,
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideBullet",
            parent=styles["BodyText"],
            fontName="Helvetica",
            fontSize=9.2,
            leading=12,
            textColor=TEXT,
            leftIndent=14,
            firstLineIndent=-8,
            bulletIndent=2,
            spaceAfter=3,
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideCode",
            parent=styles["Code"],
            fontName="Courier",
            fontSize=8.2,
            leading=10.5,
            textColor=colors.HexColor("#52616A"),
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideCaption",
            parent=styles["BodyText"],
            fontName="Helvetica-Oblique",
            fontSize=7.5,
            leading=9,
            alignment=TA_CENTER,
            textColor=MUTED,
            spaceAfter=5,
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideTableHeader",
            parent=styles["BodyText"],
            fontName="Helvetica-Bold",
            fontSize=8.2,
            leading=10,
            textColor=colors.white,
        )
    )
    styles.add(
        ParagraphStyle(
            name="GuideTableCell",
            parent=styles["BodyText"],
            fontName="Helvetica",
            fontSize=8.0,
            leading=10,
            textColor=TEXT,
        )
    )
    return styles


def code_box(text: str, styles):
    box = Table([[Paragraph(html.escape(text), styles["GuideCode"])]], colWidths=[6.55 * inch])
    box.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), CODE_BG),
                ("BOX", (0, 0), (-1, -1), 0.5, RULE),
                ("LEFTPADDING", (0, 0), (-1, -1), 7),
                ("RIGHTPADDING", (0, 0), (-1, -1), 7),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ]
        )
    )
    return box


def markdown_table(rows: list[list[str]], styles):
    width = 6.55 * inch
    first_width = width * 0.42
    data = []
    for row_index, row in enumerate(rows):
        style = styles["GuideTableHeader"] if row_index == 0 else styles["GuideTableCell"]
        data.append([Paragraph(inline_markup(cell.strip()), style) for cell in row])
    table = Table(data, colWidths=[first_width, width - first_width], repeatRows=1)
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), ACCENT),
                ("GRID", (0, 0), (-1, -1), 0.4, RULE),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("RIGHTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ]
        )
    )
    return table


def parse_markdown(source: Path, styles):
    lines = source.read_text(encoding="utf-8").splitlines()
    story = []
    paragraph: list[str] = []

    def flush_paragraph():
        if paragraph:
            story.append(
                Paragraph(inline_markup(" ".join(item.strip() for item in paragraph)), styles["GuideBody"])
            )
            paragraph.clear()

    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()

        if not stripped:
            flush_paragraph()
            index += 1
            continue

        image_match = re.fullmatch(r"!\[(.*)]\((.*)\)", stripped)
        if image_match:
            flush_paragraph()
            image_path = (source.parent / image_match.group(2)).resolve()
            image = Image(str(image_path))
            maximum_width = 6.25 * inch
            maximum_height = 3.65 * inch
            scale = min(maximum_width / image.imageWidth, maximum_height / image.imageHeight)
            image.drawWidth = image.imageWidth * scale
            image.drawHeight = image.imageHeight * scale
            story.append(
                KeepTogether(
                    [
                        image,
                        Spacer(1, 2),
                        Paragraph(html.escape(image_match.group(1)), styles["GuideCaption"]),
                    ]
                )
            )
            index += 1
            continue

        if stripped.startswith("# "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(stripped[2:]), styles["GuideTitle"]))
            index += 1
            continue
        if stripped.startswith("## "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(stripped[3:]), styles["GuideH2"]))
            index += 1
            continue
        if stripped.startswith("### "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(stripped[4:]), styles["GuideH3"]))
            index += 1
            continue

        if stripped.startswith("|") and index + 1 < len(lines) and re.match(
            r"^\s*\|(?:\s*:?-+:?\s*\|)+\s*$", lines[index + 1]
        ):
            flush_paragraph()
            rows: list[list[str]] = []
            rows.append([cell for cell in stripped.strip("|").split("|")])
            index += 2
            while index < len(lines) and lines[index].strip().startswith("|"):
                rows.append([cell for cell in lines[index].strip().strip("|").split("|")])
                index += 1
            story.append(markdown_table(rows, styles))
            story.append(Spacer(1, 5))
            continue

        bullet_match = re.match(r"^-\s+(.*)$", stripped)
        number_match = re.match(r"^(\d+)\.\s+(.*)$", stripped)
        if bullet_match or number_match:
            flush_paragraph()
            if bullet_match:
                marker = "•"
                content = bullet_match.group(1)
            else:
                marker = f"{number_match.group(1)}."
                content = number_match.group(2)
            story.append(
                Paragraph(
                    f"{html.escape(marker)}&nbsp;&nbsp;{inline_markup(content)}",
                    styles["GuideBullet"],
                )
            )
            index += 1
            continue

        if stripped.startswith("`") and stripped.endswith("`") and stripped.count("`") == 2:
            flush_paragraph()
            story.append(code_box(stripped[1:-1], styles))
            story.append(Spacer(1, 6))
            index += 1
            continue

        paragraph.append(stripped)
        index += 1

    flush_paragraph()
    return story


def decorate_page(canvas, doc):
    canvas.saveState()
    canvas.setTitle("XVatsim User Guide")
    canvas.setSubject(f"XVatsim {VERSION} Freeware User Guide")
    canvas.setAuthor("EZ SIMULATIONS")
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.5)
    canvas.line(0.62 * inch, 0.49 * inch, 7.88 * inch, 0.49 * inch)
    canvas.setFont("Helvetica", 7.2)
    canvas.setFillColor(MUTED)
    canvas.drawString(0.62 * inch, 0.30 * inch, f"XVatsim Freeware User Guide - Version {VERSION}")
    canvas.drawRightString(7.88 * inch, 0.30 * inch, f"Page {doc.page}")
    canvas.restoreState()


def main():
    parser = argparse.ArgumentParser(description="Build the XVatsim freeware user guide PDF.")
    parser.add_argument(
        "--source",
        type=Path,
        default=Path("docs/user_guide/XVatsim_User_Guide.md"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("docs/user_guide/XVatsim_User_Guide.pdf"),
    )
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    styles = build_styles()
    document = SimpleDocTemplate(
        str(output),
        pagesize=letter,
        rightMargin=0.62 * inch,
        leftMargin=0.62 * inch,
        topMargin=0.55 * inch,
        bottomMargin=0.62 * inch,
        title="XVatsim User Guide",
        author="EZ SIMULATIONS",
        subject=f"XVatsim {VERSION} Freeware User Guide",
    )
    document.build(
        parse_markdown(source, styles),
        onFirstPage=decorate_page,
        onLaterPages=decorate_page,
    )
    print(output)


if __name__ == "__main__":
    main()
