#!/usr/bin/env python3
"""Minimal SVG line chart helper with zero external dependencies."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Sequence, Tuple


COLORS = [
    "#1f77b4",
    "#d62728",
    "#2ca02c",
    "#ff7f0e",
    "#9467bd",
    "#8c564b",
]


def _escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&#39;")
    )


def write_line_chart_svg(
    output_path: Path,
    title: str,
    x_label: str,
    y_label: str,
    x_ticks: Sequence[int],
    series_data: Sequence[Tuple[str, Dict[int, float]]],
) -> None:
    width = 1024
    height = 640
    margin_left = 90
    margin_right = 260
    margin_top = 70
    margin_bottom = 100
    plot_left = margin_left
    plot_right = width - margin_right
    plot_top = margin_top
    plot_bottom = height - margin_bottom
    plot_width = plot_right - plot_left
    plot_height = plot_bottom - plot_top

    x_values = list(x_ticks)
    if not x_values:
        raise RuntimeError("x_ticks is empty")
    x_index = {value: idx for idx, value in enumerate(x_values)}

    y_values: List[float] = []
    for _, points in series_data:
        y_values.extend(points.values())
    if not y_values:
        raise RuntimeError("No y values to plot")

    y_max = max(y_values)
    if y_max <= 0:
        y_max = 1.0
    y_max *= 1.15

    def x_to_px(x: int) -> float:
        if len(x_values) == 1:
            return plot_left + plot_width / 2
        idx = x_index[x]
        return plot_left + idx * (plot_width / (len(x_values) - 1))

    def y_to_px(y: float) -> float:
        return plot_bottom - (y / y_max) * plot_height

    lines: List[str] = []
    lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">')
    lines.append('<rect x="0" y="0" width="100%" height="100%" fill="#ffffff"/>')
    lines.append(
        f'<text x="{width / 2:.1f}" y="36" text-anchor="middle" font-size="24" font-family="Arial">{_escape(title)}</text>'
    )

    # Grid and y ticks.
    y_tick_count = 6
    for i in range(y_tick_count + 1):
        value = y_max * i / y_tick_count
        y = y_to_px(value)
        lines.append(f'<line x1="{plot_left}" y1="{y:.2f}" x2="{plot_right}" y2="{y:.2f}" stroke="#d9d9d9" stroke-width="1"/>')
        lines.append(
            f'<text x="{plot_left - 10}" y="{y + 5:.2f}" text-anchor="end" font-size="13" font-family="Arial">{value:.1f}</text>'
        )

    # Axes.
    lines.append(f'<line x1="{plot_left}" y1="{plot_bottom}" x2="{plot_right}" y2="{plot_bottom}" stroke="#000000" stroke-width="2"/>')
    lines.append(f'<line x1="{plot_left}" y1="{plot_top}" x2="{plot_left}" y2="{plot_bottom}" stroke="#000000" stroke-width="2"/>')

    # X ticks.
    for value in x_values:
        x = x_to_px(value)
        lines.append(f'<line x1="{x:.2f}" y1="{plot_bottom}" x2="{x:.2f}" y2="{plot_bottom + 6}" stroke="#000000" stroke-width="1"/>')
        lines.append(
            f'<text x="{x:.2f}" y="{plot_bottom + 28}" text-anchor="middle" font-size="13" font-family="Arial">{value}</text>'
        )

    # Series.
    legend_x = plot_right + 22
    legend_y = plot_top + 22
    for idx, (label, points) in enumerate(series_data):
        color = COLORS[idx % len(COLORS)]
        sorted_points = sorted(points.items(), key=lambda pair: pair[0])
        poly_points = " ".join(f"{x_to_px(x):.2f},{y_to_px(y):.2f}" for x, y in sorted_points)
        lines.append(f'<polyline fill="none" stroke="{color}" stroke-width="3" points="{poly_points}"/>')
        for x, y in sorted_points:
            lines.append(f'<circle cx="{x_to_px(x):.2f}" cy="{y_to_px(y):.2f}" r="4" fill="{color}"/>')

        ly = legend_y + idx * 28
        lines.append(f'<line x1="{legend_x}" y1="{ly}" x2="{legend_x + 26}" y2="{ly}" stroke="{color}" stroke-width="3"/>')
        lines.append(
            f'<text x="{legend_x + 34}" y="{ly + 5}" text-anchor="start" font-size="14" font-family="Arial">{_escape(label)}</text>'
        )

    # Axis labels.
    lines.append(
        f'<text x="{(plot_left + plot_right) / 2:.1f}" y="{height - 25}" text-anchor="middle" font-size="16" font-family="Arial">{_escape(x_label)}</text>'
    )
    lines.append(
        f'<text x="28" y="{(plot_top + plot_bottom) / 2:.1f}" text-anchor="middle" font-size="16" font-family="Arial" transform="rotate(-90 28 {(plot_top + plot_bottom) / 2:.1f})">{_escape(y_label)}</text>'
    )
    lines.append("</svg>")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")


def _format_tick(value: float) -> str:
    if abs(value) >= 100:
        return f"{value:.0f}"
    if abs(value) >= 10:
        return f"{value:.1f}"
    return f"{value:.2f}"


def write_xy_line_chart_svg(
    output_path: Path,
    title: str,
    x_label: str,
    y_label: str,
    series_points: Sequence[Tuple[str, Sequence[Tuple[float, float]]]],
) -> None:
    width = 1024
    height = 640
    margin_left = 90
    margin_right = 260
    margin_top = 70
    margin_bottom = 100
    plot_left = margin_left
    plot_right = width - margin_right
    plot_top = margin_top
    plot_bottom = height - margin_bottom
    plot_width = plot_right - plot_left
    plot_height = plot_bottom - plot_top

    x_values: List[float] = []
    y_values: List[float] = []
    for _, points in series_points:
        for x, y in points:
            x_values.append(float(x))
            y_values.append(float(y))

    if not x_values or not y_values:
        raise RuntimeError("No points to plot")

    x_min = min(x_values)
    x_max = max(x_values)
    y_min = min(y_values)
    y_max = max(y_values)

    if x_max <= x_min:
        x_min -= 1.0
        x_max += 1.0
    if y_min > 0:
        y_min = 0.0
    if y_max <= y_min:
        y_max = y_min + 1.0
    y_max *= 1.10

    def x_to_px(x: float) -> float:
        return plot_left + ((x - x_min) / (x_max - x_min)) * plot_width

    def y_to_px(y: float) -> float:
        return plot_bottom - ((y - y_min) / (y_max - y_min)) * plot_height

    lines: List[str] = []
    lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">')
    lines.append('<rect x="0" y="0" width="100%" height="100%" fill="#ffffff"/>')
    lines.append(
        f'<text x="{width / 2:.1f}" y="36" text-anchor="middle" font-size="24" font-family="Arial">{_escape(title)}</text>'
    )

    y_tick_count = 6
    for i in range(y_tick_count + 1):
        value = y_min + (y_max - y_min) * i / y_tick_count
        y = y_to_px(value)
        lines.append(f'<line x1="{plot_left}" y1="{y:.2f}" x2="{plot_right}" y2="{y:.2f}" stroke="#d9d9d9" stroke-width="1"/>')
        lines.append(
            f'<text x="{plot_left - 10}" y="{y + 5:.2f}" text-anchor="end" font-size="13" font-family="Arial">{_format_tick(value)}</text>'
        )

    x_tick_count = 6
    for i in range(x_tick_count + 1):
        value = x_min + (x_max - x_min) * i / x_tick_count
        x = x_to_px(value)
        lines.append(f'<line x1="{x:.2f}" y1="{plot_top}" x2="{x:.2f}" y2="{plot_bottom}" stroke="#eeeeee" stroke-width="1"/>')
        lines.append(f'<line x1="{x:.2f}" y1="{plot_bottom}" x2="{x:.2f}" y2="{plot_bottom + 6}" stroke="#000000" stroke-width="1"/>')
        lines.append(
            f'<text x="{x:.2f}" y="{plot_bottom + 28}" text-anchor="middle" font-size="13" font-family="Arial">{_format_tick(value)}</text>'
        )

    lines.append(f'<line x1="{plot_left}" y1="{plot_bottom}" x2="{plot_right}" y2="{plot_bottom}" stroke="#000000" stroke-width="2"/>')
    lines.append(f'<line x1="{plot_left}" y1="{plot_top}" x2="{plot_left}" y2="{plot_bottom}" stroke="#000000" stroke-width="2"/>')

    legend_x = plot_right + 22
    legend_y = plot_top + 22
    for idx, (label, points) in enumerate(series_points):
        if not points:
            continue
        color = COLORS[idx % len(COLORS)]
        sorted_points = sorted(points, key=lambda pair: pair[0])
        poly_points = " ".join(f"{x_to_px(x):.2f},{y_to_px(y):.2f}" for x, y in sorted_points)
        lines.append(f'<polyline fill="none" stroke="{color}" stroke-width="3" points="{poly_points}"/>')
        for x, y in sorted_points:
            lines.append(f'<circle cx="{x_to_px(x):.2f}" cy="{y_to_px(y):.2f}" r="4" fill="{color}"/>')

        ly = legend_y + idx * 28
        lines.append(f'<line x1="{legend_x}" y1="{ly}" x2="{legend_x + 26}" y2="{ly}" stroke="{color}" stroke-width="3"/>')
        lines.append(
            f'<text x="{legend_x + 34}" y="{ly + 5}" text-anchor="start" font-size="14" font-family="Arial">{_escape(label)}</text>'
        )

    lines.append(
        f'<text x="{(plot_left + plot_right) / 2:.1f}" y="{height - 25}" text-anchor="middle" font-size="16" font-family="Arial">{_escape(x_label)}</text>'
    )
    lines.append(
        f'<text x="28" y="{(plot_top + plot_bottom) / 2:.1f}" text-anchor="middle" font-size="16" font-family="Arial" transform="rotate(-90 28 {(plot_top + plot_bottom) / 2:.1f})">{_escape(y_label)}</text>'
    )
    lines.append("</svg>")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
