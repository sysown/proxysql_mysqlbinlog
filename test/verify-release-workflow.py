#!/usr/bin/env python3

from pathlib import Path
import sys

import yaml


EXPECTED_DISTROS = ["centos9", "centos10", "debian12", "debian13", "ubuntu22", "ubuntu24"]
BUILD_TAG = "proxysql/proxysql-mysqlbinlog:build-${{ matrix.distro }}"
BUILD_FILE = "docker/build/build-${{ matrix.distro }}/Dockerfile"
PACKAGE_COMMAND = "make ${{ matrix.distro }}"
CI_WORKFLOW_PATH = Path(".github/workflows/ci-test.yml")
CI_STEP_NAME = "Verify release package workflow"
CI_COMMAND = "python3 test/verify-release-workflow.py"


def fail(message):
    raise AssertionError(message)


def main():
    workflow_path = Path(sys.argv[1] if len(sys.argv) > 1 else ".github/workflows/release.yml")
    workflow = yaml.safe_load(workflow_path.read_text(encoding="utf-8"))

    try:
        package_job = workflow["jobs"]["package"]
        distros = package_job["strategy"]["matrix"]["distro"]
        steps = package_job["steps"]
    except (KeyError, TypeError) as error:
        fail(f"package job structure is incomplete: {error}")

    if distros != EXPECTED_DISTROS:
        fail(f"package distro matrix is {distros!r}, expected {EXPECTED_DISTROS!r}")

    package_indexes = [
        index
        for index, step in enumerate(steps)
        if step.get("run", "").strip() == PACKAGE_COMMAND
    ]
    if len(package_indexes) != 1:
        fail(f"expected exactly one package command {PACKAGE_COMMAND!r}")

    checkout_indexes = [
        index
        for index, step in enumerate(steps)
        if step.get("uses", "").startswith("actions/checkout@")
    ]
    if len(checkout_indexes) != 1:
        fail("expected exactly one checkout step in the package job")

    build_indexes = []
    for index, step in enumerate(steps):
        run = step.get("run", "")
        run_lines = [line.strip().rstrip("\\").strip() for line in run.splitlines()]
        if (
            "docker build" in run
            and BUILD_TAG in run
            and BUILD_FILE in run
            and "." in run_lines
        ):
            build_indexes.append(index)

    if len(build_indexes) != 1:
        fail("expected exactly one local toolchain-image build for the package matrix")

    if build_indexes[0] >= package_indexes[0]:
        fail("the local toolchain image must be built before package creation")

    if checkout_indexes[0] >= build_indexes[0]:
        fail("the local toolchain image must use the checked-out release source")

    ci_workflow = yaml.safe_load(CI_WORKFLOW_PATH.read_text(encoding="utf-8"))
    try:
        ci_steps = ci_workflow["jobs"]["packages"]["steps"]
    except (KeyError, TypeError) as error:
        fail(f"package CI job structure is incomplete: {error}")

    ci_checkout_indexes = [
        index
        for index, step in enumerate(ci_steps)
        if step.get("uses", "").startswith("actions/checkout@")
    ]
    if len(ci_checkout_indexes) != 1:
        fail("expected exactly one checkout step in the package CI job")

    expected_ci_step = {"name": CI_STEP_NAME, "run": CI_COMMAND}
    if ci_steps[ci_checkout_indexes[0] + 1] != expected_ci_step:
        fail("release workflow validation must run immediately after package CI checkout")


if __name__ == "__main__":
    main()
