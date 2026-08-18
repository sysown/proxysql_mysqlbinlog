#!/usr/bin/env ruby

require "yaml"

EXPECTED_DISTROS = %w[centos9 centos10 debian12 debian13 ubuntu22 ubuntu24].freeze
BUILD_TAG = "proxysql/proxysql-mysqlbinlog:build-${{ matrix.distro }}"
BUILD_FILE = "docker/build/build-${{ matrix.distro }}/Dockerfile"
PACKAGE_COMMAND = "make ${{ matrix.distro }}"
CHECKER_COMMAND = "ruby test/verify-release-workflow.rb"
PACKAGE_VERIFIER_COMMAND = 'test/verify-package-contents.sh "${packages[0]}"'
RUNNER_IMAGE = "proxysql/proxysql-mysqlbinlog:build-ubuntu24"
RUNNER_DOCKERFILE = "docker/build/build-ubuntu24/Dockerfile"
MYSQL_TEST_STEPS = {
  "Test MySQL 5.7" => "57",
  "Test MySQL 8.0" => "80",
  "Test MySQL 8.4" => "84",
  "Test MySQL 9.0" => "90",
  "Test MySQL 9.4" => "94"
}.freeze


def fail(message)
  raise message
end


def load_workflow(path)
  YAML.safe_load(File.read(path), aliases: true)
rescue Psych::Exception => error
  fail("cannot parse #{path}: #{error.message}")
end


def package_job(workflow, label)
  workflow.fetch("jobs").fetch("package")
rescue KeyError => error
  fail("#{label} package job structure is incomplete: #{error.message}")
end


def matrix(job, key, label)
  job.fetch("strategy").fetch("matrix").fetch(key)
rescue KeyError => error
  fail("#{label} matrix is incomplete: #{error.message}")
end


def steps(job, label)
  job.fetch("steps")
rescue KeyError => error
  fail("#{label} steps are missing: #{error.message}")
end


def step_index(steps, name)
  indexes = steps.each_index.select { |index| steps[index]["name"] == name }
  fail("expected exactly one #{name.inspect} step") unless indexes.length == 1

  indexes.first
end


def line_index(lines, line, label)
  indexes = lines.each_index.select { |index| lines[index] == line }
  fail("expected exactly one #{line.inspect} line in #{label}") unless indexes.length == 1

  indexes.first
end


def shell_commands(command)
  commands = []
  current = []

  command.each_line do |line|
    stripped = line.strip.sub(/\s+#.*\z/, "")
    next if stripped.empty? || stripped.start_with?("#")

    continues = stripped.end_with?("\\")
    current << stripped.delete_suffix("\\").strip
    next if continues

    commands << current.join(" ")
    current = []
  end

  commands << current.join(" ") unless current.empty?
  commands
end


def runner_build_command?(command)
  shell_commands(command).include?("docker build -t \"$RUNNER_IMG\" -f #{RUNNER_DOCKERFILE} .")
end


def tap_run_command?(command)
  shell_commands(command).any? do |shell_command|
    shell_command.start_with?("docker run ") && shell_command.end_with?('"$RUNNER_IMG" make')
  end
end


def assert_matrix(actual, label)
  fail("#{label} is #{actual.inspect}, expected #{EXPECTED_DISTROS.inspect}") unless actual == EXPECTED_DISTROS
end


def assert_single_package_enumeration(command, label)
  lines = command.lines.map(&:strip).reject(&:empty?)
  nullglob_index = line_index(lines, "shopt -s nullglob", label)
  rpm_index = line_index(lines, "centos*) packages=(binaries/*.rpm) ;;", label)
  deb_index = line_index(lines, "*) packages=(binaries/*.deb) ;;", label)
  guard_index = line_index(lines, "if [[ ${#packages[@]} -ne 1 ]]; then", label)
  invocation_index = line_index(lines, PACKAGE_VERIFIER_COMMAND, label)
  guard_exit_index = lines.each_index.find do |index|
    index > guard_index && lines[index] == "exit 1"
  end
  guard_end_index = lines.each_index.find do |index|
    index > guard_index && lines[index] == "fi"
  end

  fail("#{label} does not reject an unexpected package count") if guard_exit_index.nil?
  fail("#{label} does not close its package-count guard") if guard_end_index.nil?
  unless nullglob_index < rpm_index && nullglob_index < deb_index && rpm_index < guard_index && deb_index < guard_index && guard_index < guard_exit_index && guard_exit_index < guard_end_index && guard_end_index < invocation_index
    fail("#{label} must enumerate and reject unexpected package counts before verification")
  end
end


def assert_release_package_contract(release)
  job = package_job(release, "release")
  assert_matrix(matrix(job, "distro", "release package"), "release package matrix")
  package_steps = steps(job, "release package")

  build_indexes = package_steps.each_index.select do |index|
    run = package_steps[index].fetch("run", "")
    lines = run.lines.map { |line| line.strip.delete_suffix("\\").strip }
    run.include?("docker build") && run.include?(BUILD_TAG) && run.include?(BUILD_FILE) && lines.include?(".")
  end
  fail("expected exactly one local toolchain-image build for the release package matrix") unless build_indexes.length == 1

  package_indexes = package_steps.each_index.select do |index|
    package_steps[index].fetch("run", "").strip == PACKAGE_COMMAND
  end
  fail("expected exactly one release package command #{PACKAGE_COMMAND.inspect}") unless package_indexes.length == 1

  checkout_indexes = package_steps.each_index.select do |index|
    package_steps[index].fetch("uses", "").start_with?("actions/checkout@")
  end
  fail("expected exactly one release package checkout step") unless checkout_indexes.length == 1
  unless checkout_indexes.first < build_indexes.first && build_indexes.first < package_indexes.first
    fail("release packaging must check out source before building the toolchain image before package creation")
  end

  verify_index = step_index(package_steps, "Verify package contents")
  verify_command = package_steps[verify_index].fetch("run", "")
  fail("release package verification does not run the package contents checker") unless verify_command.include?("test/verify-package-contents.sh")
  assert_single_package_enumeration(verify_command, "release package verification")

  list_index = step_index(package_steps, "List built artifacts")
  upload_artifact_index = step_index(package_steps, "Upload package artifact")
  upload_release_index = step_index(package_steps, "Upload package to GitHub release")
  unless package_indexes.first < verify_index && verify_index < list_index && verify_index < upload_artifact_index && verify_index < upload_release_index
    fail("release package contents must be verified after packaging and before artifact handling")
  end
end


def assert_release_container_contract(release)
  job = release.fetch("jobs").fetch("container-build")
  assert_matrix(matrix(job, "distro", "release container-build"), "release container-build matrix")
end


def assert_release_test_images_contract(release)
  job = release.fetch("jobs").fetch("test-images")
  fail("release test-images RUNNER_IMG is not #{RUNNER_IMAGE.inspect}") unless job.fetch("env").fetch("RUNNER_IMG") == RUNNER_IMAGE
  test_steps = steps(job, "release test-images")

  checkout_indexes = test_steps.each_index.select do |index|
    test_steps[index].fetch("uses", "").start_with?("actions/checkout@")
  end
  fail("expected exactly one release test-images checkout step") unless checkout_indexes.length == 1
  checkout_with = test_steps[checkout_indexes.first]["with"]
  checkout_ref = checkout_with.is_a?(Hash) ? checkout_with["ref"] : nil
  fail("release test-images checkout must use the release tag") unless checkout_ref == "${{ github.event.release.tag_name }}"

  runner_build_indexes = test_steps.each_index.select do |index|
    runner_build_command?(test_steps[index].fetch("run", ""))
  end
  fail("expected exactly one RUNNER_IMG toolchain build for release test-images") unless runner_build_indexes.length == 1

  tap_index = step_index(test_steps, "Build TAP test binaries")
  tap_command = test_steps[tap_index].fetch("run", "")
  unless tap_run_command?(tap_command)
    fail("release TAP build does not run in RUNNER_IMG")
  end
  unless checkout_indexes.first < runner_build_indexes.first && runner_build_indexes.first < tap_index
    fail("release test-images must check out source before building RUNNER_IMG before TAP compilation")
  end
rescue KeyError => error
  fail("release test-images job structure is incomplete: #{error.message}")
end


def assert_mysql_test_step(step, name, version)
  fail("#{name} must continue unless the workflow is cancelled") unless step["if"] == "${{ !cancelled() }}"
  fail("#{name} must time out after 30 minutes") unless step["timeout-minutes"] == 30
  fail("#{name} must not use continue-on-error") if step.key?("continue-on-error")

  environment = step["env"]
  mysql_versions = environment.is_a?(Hash) ? environment.fetch("MYSQL_VERSIONS", "").to_s : ""
  fail("#{name} must set MYSQL_VERSIONS to only #{version}") unless mysql_versions == version

  commands = shell_commands(step.fetch("run", ""))
  fail("#{name} must exit when a test command fails") unless commands.include?("set -e")
  fail("#{name} must define a Docker Compose cleanup function") unless commands.include?("cleanup() { docker compose down -v 2>/dev/null; }")
  fail("#{name} must run Docker Compose cleanup on exit") unless commands.include?("trap cleanup EXIT")
  fail("#{name} must start its MySQL service") unless commands.include?("docker compose up -d mysql")
  fail("#{name} must run the TAP runner") unless commands.include?("docker compose run --rm runner")
end


def assert_ci_contract(ci)
  jobs = ci.fetch("jobs")
  packages_job = jobs.fetch("packages")
  assert_matrix(matrix(packages_job, "target", "CI packages"), "CI packages matrix")
  package_steps = steps(packages_job, "CI packages")
  package_verify_index = step_index(package_steps, "Verify package contents")
  assert_single_package_enumeration(package_steps[package_verify_index].fetch("run", ""), "CI package verification")

  contract_job = jobs.fetch("workflow-contract")
  contract_steps = steps(contract_job, "workflow-contract")
  checkout_index = step_index(contract_steps, "Check out source")
  checker_steps = contract_steps.each_index.select do |index|
    contract_steps[index].fetch("run", "").strip == CHECKER_COMMAND
  end
  fail("expected exactly one workflow-contract checker step") unless checker_steps.length == 1
  fail("workflow-contract checker must run after checkout") unless checkout_index < checker_steps.first

  checker_count = jobs.values.sum do |job|
    Array(job["steps"]).count { |step| step.fetch("run", "").strip == CHECKER_COMMAND }
  end
  fail("expected the workflow checker to run exactly once") unless checker_count == 1

  %w[packages test].each do |job_name|
    needs = jobs.fetch(job_name).fetch("needs", [])
    needs = [needs] unless needs.is_a?(Array)
    fail("#{job_name} must need workflow-contract") unless needs.include?("workflow-contract")
  end

  test_job = jobs.fetch("test")
  fail("CI test job must not use continue-on-error") if test_job.key?("continue-on-error")

  test_steps = steps(test_job, "CI test")
  mysql_test_names = test_steps.filter_map do |step|
    name = step["name"]
    name if name.is_a?(String) && name.start_with?("Test MySQL ")
  end
  fail("CI test must contain exactly #{MYSQL_TEST_STEPS.keys.inspect} MySQL test steps") unless mysql_test_names == MYSQL_TEST_STEPS.keys

  MYSQL_TEST_STEPS.each do |name, version|
    assert_mysql_test_step(test_steps.fetch(step_index(test_steps, name)), name, version)
  end

  upload_logs = test_steps.fetch(step_index(test_steps, "Upload test logs"))
  fail("Upload test logs must run even when a test fails") unless upload_logs["if"] == "always()"
rescue KeyError => error
  fail("CI workflow structure is incomplete: #{error.message}")
end


release_path = ARGV[0] || ".github/workflows/release.yml"
ci_path = ARGV[1] || ".github/workflows/ci-test.yml"
release = load_workflow(release_path)
ci = load_workflow(ci_path)

assert_release_package_contract(release)
assert_release_container_contract(release)
assert_release_test_images_contract(release)
assert_ci_contract(ci)
