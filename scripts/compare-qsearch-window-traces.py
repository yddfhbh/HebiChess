#!/usr/bin/env python3
"""Report the first causal difference in two QTT-off qsearch window traces.

The input is the TSV emitted by ``HebiChessSearchQttActiveProfile`` with two
``--qsearch-window`` requests.  Nodes are aligned by full position key and
ply; candidate decisions are then compared by UCI move.  The order is the
store-window preorder, so an earlier pruning decision is reported before its
downstream score/return differences.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from typing import Any


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--store-window", default="store")
    parser.add_argument("--hit-window", default="hit")
    return parser.parse_args()


def load(path: Path) -> dict[str, list[dict[str, Any]]]:
    header: list[str] | None = None
    nodes: dict[str, dict[tuple[str, str], dict[str, Any]]] = defaultdict(dict)
    for row in csv.reader(path.read_text(encoding="utf-8").splitlines(), delimiter="\t"):
        if not row:
            continue
        if row[0].startswith("#"):
            header = [value.removeprefix("# ") for value in row]
            continue
        if header is None or len(row) != len(header):
            raise RuntimeError("malformed qsearch window trace")
        value = dict(zip(header, row))
        key = (value["key"], value["ply"])
        bucket = nodes[value["window"]].setdefault(key, {"node": None, "moves": {}})
        if value["record"] == "node":
            bucket["node"] = value
        elif value["record"] == "move":
            bucket["moves"][value["move"]] = value
        else:
            raise RuntimeError(f"unknown trace record: {value['record']}")
    result: dict[str, list[dict[str, Any]]] = {}
    for window, table in nodes.items():
        ordered = sorted(table.values(), key=lambda item: int(item["node"]["sequence"])
                         if item["node"] is not None else 1 << 60)
        result[window] = ordered
    return result


def difference(kind: str, store: dict[str, Any], hit: dict[str, Any],
               move: str | None = None, fields: tuple[str, ...] = ()) -> dict[str, Any]:
    store_node = store.get("node", store)
    hit_node = hit.get("node", hit)
    node = store_node or hit_node
    return {
        "kind": kind,
        "key": node["key"],
        "ply": int(node["ply"]),
        "fen": node["fen"],
        "move": move,
        "fields": {field: {"store": store[field], "hit": hit[field]} for field in fields},
        "store_node": store_node,
        "hit_node": hit_node,
    }


def compare(store_nodes: list[dict[str, Any]], hit_nodes: list[dict[str, Any]]) -> dict[str, Any]:
    hit_by_key = {(item["node"]["key"], item["node"]["ply"]): item
                  for item in hit_nodes if item["node"] is not None}
    for store in store_nodes:
        store_node = store["node"]
        if store_node is None:
            continue
        identity = (store_node["key"], store_node["ply"])
        hit = hit_by_key.get(identity)
        if hit is None:
            return difference("node-missing", store, {"node": store_node}, fields=())
        hit_node = hit["node"]
        for kind, fields in (
            ("fen", ("fen",)),
            ("raw-nnue", ("raw_nnue",)),
            ("accumulator-checksum", ("accumulator_checksum",)),
        ):
            if any(store_node[field] != hit_node[field] for field in fields):
                return difference(kind, store_node, hit_node, fields=fields)
        if store_node["in_check"] != hit_node["in_check"]:
            return difference("check-state", store_node, hit_node, fields=("in_check",))
        store_moves, hit_moves = store["moves"], hit["moves"]
        if set(store_moves) != set(hit_moves):
            return difference("generated-move-set", store_node, hit_node, fields=()) | {
                "store_moves": sorted(store_moves), "hit_moves": sorted(hit_moves)}
        for move in sorted(store_moves, key=lambda value: int(store_moves[value]["order"])):
            left, right = store_moves[move], hit_moves[move]
            for kind, fields in (
                ("move-order", ("order",)),
                ("see-decision", ("see_rejected",)),
                ("delta-pruning", ("delta_rejected",)),
                ("searched-move-set", ("searched",)),
            ):
                if any(left[field] != right[field] for field in fields):
                    return difference(kind, left, right, move, fields)
            if left["searched"] == "1" and right["searched"] == "1":
                same_parent_window = (store_node["entry_alpha"], store_node["entry_beta"]) == (
                    hit_node["entry_alpha"], hit_node["entry_beta"])
                if same_parent_window and (left["child_alpha"], left["child_beta"]) != (
                        right["child_alpha"], right["child_beta"]):
                    # Different initial windows naturally produce different
                    # child windows.  Only flag a changed child window after
                    # the aligned parent window itself has converged.
                    return difference("child-window", left, right, move,
                                      ("child_alpha", "child_beta"))
                if same_parent_window and left["beta_cutoff"] != right["beta_cutoff"]:
                    return difference("beta-cutoff", left, right, move, ("beta_cutoff",))
                if same_parent_window and left["alpha_after"] != right["alpha_after"]:
                    return difference("alpha-update", left, right, move, ("alpha_after",))
        if store_node["return_kind"] != hit_node["return_kind"]:
            return difference("early-return", store_node, hit_node, fields=("return_kind",))
        if store_node["returned_score"] != hit_node["returned_score"]:
            return difference("final-return", store_node, hit_node, fields=("returned_score",))
    return {"kind": "no-decision-divergence"}


def main() -> int:
    options = arguments()
    windows = load(options.trace)
    if options.store_window not in windows or options.hit_window not in windows:
        raise RuntimeError("trace does not contain both requested window labels")
    print(json.dumps(compare(windows[options.store_window], windows[options.hit_window]),
                     ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
