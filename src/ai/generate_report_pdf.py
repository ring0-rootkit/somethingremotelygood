#!/usr/bin/env python3
"""Generate a PDF report from a JSON analysis report file."""

import argparse
import json
import os
import sys

try:
    from fpdf import FPDF
except ImportError:
    print("ERROR: fpdf2 not installed. Run: pip install fpdf2")
    sys.exit(1)


RISK_COLORS = {
    "benign": (46, 125, 50),
    "suspicious": (245, 124, 0),
    "malicious": (198, 40, 40),
}


class ReportPDF(FPDF):

    def header(self):
        self.set_font("Helvetica", "B", 16)
        self.cell(0, 10, "Security Analysis Report", align="C", new_x="LMARGIN", new_y="NEXT")
        self.ln(2)

    def footer(self):
        self.set_y(-15)
        self.set_font("Helvetica", "I", 8)
        self.set_text_color(128, 128, 128)
        self.cell(0, 10, f"Page {self.page_no()}/{{nb}}", align="C")

    def section_title(self, title):
        self.set_font("Helvetica", "B", 13)
        self.set_text_color(33, 33, 33)
        self.set_fill_color(230, 230, 230)
        self.cell(0, 8, f"  {title}", fill=True, new_x="LMARGIN", new_y="NEXT")
        self.ln(2)

    def label_value(self, label, value):
        self.set_font("Helvetica", "B", 10)
        self.set_text_color(80, 80, 80)
        self.cell(45, 6, f"{label}:")
        self.set_font("Helvetica", "", 10)
        self.set_text_color(33, 33, 33)
        self.multi_cell(0, 6, str(value), new_x="LMARGIN", new_y="NEXT")

    def risk_badge(self, risk_level):
        r, g, b = RISK_COLORS.get(risk_level, (128, 128, 128))
        self.set_font("Helvetica", "B", 12)
        self.set_text_color(255, 255, 255)
        self.set_fill_color(r, g, b)
        self.cell(50, 8, f"  {risk_level.upper()}  ", fill=True, new_x="LMARGIN", new_y="NEXT")
        self.set_text_color(33, 33, 33)
        self.ln(3)


def generate_pdf(json_path, output_path=None):
    with open(json_path) as f:
        data = json.load(f)

    if not output_path:
        output_path = json_path.rsplit(".", 1)[0] + ".pdf"

    meta = data.get("report_metadata", {})
    user = data.get("user", {})
    anomaly = data.get("anomaly", {})
    analysis = data.get("command_analysis", {})
    commands = data.get("command_history", [])

    pdf = ReportPDF()
    pdf.alias_nb_pages()
    pdf.set_auto_page_break(auto=True, margin=20)
    pdf.add_page()

    # Metadata
    pdf.section_title("Report Metadata")
    pdf.label_value("Generated", meta.get("generated_at", "N/A"))
    pdf.label_value("Anomaly ID", meta.get("anomaly_report_id", "N/A"))
    pdf.label_value("LLM Model", meta.get("model", "N/A"))
    pdf.ln(3)

    # User info
    pdf.section_title("User Information")
    pdf.label_value("User ID", user.get("user_id", "N/A"))
    pdf.label_value("Container ID", user.get("container_id", "N/A"))
    pdf.ln(3)

    # Anomaly details
    pdf.section_title("Anomaly Detection (AI 1)")
    pdf.label_value("Type", anomaly.get("type", "N/A"))
    pdf.label_value("Severity", anomaly.get("severity", "N/A"))
    pdf.label_value("Summary", anomaly.get("summary", "N/A"))
    details = anomaly.get("details", [])
    if details:
        pdf.ln(2)
        pdf.set_font("Helvetica", "B", 10)
        pdf.cell(0, 6, "Anomaly Details:", new_x="LMARGIN", new_y="NEXT")
        pdf.set_font("Courier", "", 8)
        for d in details:
            detail_type = d.get("type", "unknown")
            detail_text = d.get("detail", str(d))
            z = d.get("z_score", "")
            pdf.multi_cell(0, 4, f"  [{detail_type}] {detail_text} (z={z})", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(3)

    # Command analysis
    pdf.section_title("Command Analysis (AI 2)")
    risk = analysis.get("risk_level", "unknown")
    pdf.risk_badge(risk)
    pdf.label_value("Summary", analysis.get("summary", "N/A"))
    pdf.label_value("Recommendation", analysis.get("recommendation", "N/A"))
    pdf.label_value("Commands Analyzed", data.get("commands_analyzed", 0))

    findings = analysis.get("findings", [])
    if findings:
        pdf.ln(3)
        pdf.set_font("Helvetica", "B", 10)
        pdf.cell(0, 6, "Findings:", new_x="LMARGIN", new_y="NEXT")
        pdf.ln(1)

        # Table header
        pdf.set_font("Helvetica", "B", 9)
        pdf.set_fill_color(200, 200, 200)
        pdf.cell(15, 6, "Line", border=1, fill=True)
        pdf.cell(45, 6, "Category", border=1, fill=True)
        pdf.cell(0, 6, "Concern", border=1, fill=True, new_x="LMARGIN", new_y="NEXT")

        pdf.set_font("Helvetica", "", 8)
        for finding in findings:
            line = str(finding.get("line", "?"))
            cat = str(finding.get("category", ""))[:30]
            concern = str(finding.get("concern", ""))
            y_before = pdf.get_y()
            pdf.cell(15, 6, line, border=1)
            pdf.cell(45, 6, cat, border=1)
            pdf.multi_cell(0, 6, concern, border=1, new_x="LMARGIN", new_y="NEXT")
    pdf.ln(3)

    # Command history
    pdf.section_title("Command History (sanitized)")
    pdf.set_font("Courier", "", 7)
    for i, cmd in enumerate(commands[:200], 1):
        line = f"{i:>4}| {cmd}"
        if len(line) > 120:
            line = line[:117] + "..."
        # Highlight suspicious lines
        if any(kw in cmd.lower() for kw in [
            "/etc/shadow", "chmod 777", "nc -l", "reverse", "backdoor",
            "nmap", "payload", "/etc/sudoers", "useradd", "[FILTERED"
        ]):
            pdf.set_text_color(198, 40, 40)
        else:
            pdf.set_text_color(33, 33, 33)
        pdf.cell(0, 3.5, line, new_x="LMARGIN", new_y="NEXT")
    if len(commands) > 200:
        pdf.set_text_color(128, 128, 128)
        pdf.cell(0, 4, f"  ... and {len(commands) - 200} more commands", new_x="LMARGIN", new_y="NEXT")

    pdf.set_text_color(33, 33, 33)
    pdf.output(output_path)
    print(f"PDF report saved to {output_path}")
    return output_path


def main():
    parser = argparse.ArgumentParser(description="Generate PDF from JSON analysis report")
    parser.add_argument("json_file", help="Path to JSON report file")
    parser.add_argument("-o", "--output", help="Output PDF path (default: same name as JSON with .pdf)")
    args = parser.parse_args()

    if not os.path.exists(args.json_file):
        print(f"File not found: {args.json_file}")
        sys.exit(1)

    generate_pdf(args.json_file, args.output)


if __name__ == "__main__":
    main()
