#!/usr/bin/env ruby

require "yaml"

EXPECTED_DISTROS = %w[centos9 centos10 debian12 debian13 ubuntu22 ubuntu24].freeze
AMD64_RUNNER = "ubuntu-latest"
ARM64_RUNNER = "ubuntu-24.04-arm"
EXPECTED_RELEASE_BUILD_MATRIX = [
  {"distro" => "centos9", "arch" => "amd64", "runner" => AMD64_RUNNER},
  {"distro" => "centos10", "arch" => "amd64", "runner" => AMD64_RUNNER},
  {"distro" => "debian12", "arch" => "amd64", "runner" => AMD64_RUNNER},
  {"distro" => "debian13", "arch" => "amd64", "runner" => AMD64_RUNNER},
  {"distro" => "ubuntu22", "arch" => "amd64", "runner" => AMD64_RUNNER},
  {"distro" => "ubuntu24", "arch" => "amd64", "runner" => AMD64_RUNNER},
  {"distro" => "debian13", "arch" => "arm64", "runner" => ARM64_RUNNER},
  {"distro" => "ubuntu24", "arch" => "arm64", "runner" => ARM64_RUNNER}
].freeze
EXPECTED_CI_PACKAGE_MATRIX = EXPECTED_RELEASE_BUILD_MATRIX.map do |entry|
  {"target" => entry.fetch("distro"), "arch" => entry.fetch("arch"), "runner" => entry.fetch("runner")}
end.freeze
BUILD_TAG = "proxysql/proxysql-mysqlbinlog:build-${{ matrix.distro }}"
BUILD_FILE = "docker/build/build-${{ matrix.distro }}/Dockerfile"
PACKAGE_COMMAND = "make ${{ matrix.distro }}"
CHECKER_COMMAND = "ruby test/verify-release-workflow.rb"
GHCR_ACCESS_CHECKER_STEP = "Verify GHCR access checker"
GHCR_ACCESS_CHECKER_COMMAND = "test/verify-ghcr-public-access-test.sh"
RUNTIME_IMAGE_CONFIGURATION_STEP = "Verify runtime image configuration"
RUNTIME_IMAGE_CONFIGURATION_COMMAND = "test/verify-runtime-tls-env.sh"
LIBDAEMON_ARM64_CONFIGURATION_STEP = "Verify libdaemon ARM64 bootstrap"
LIBDAEMON_ARM64_CONFIGURATION_COMMAND = "test/verify-libdaemon-aarch64-config.sh"
PACKAGE_VERIFIER_COMMAND = 'test/verify-package-contents.sh "${packages[0]}"'
PACKAGE_METADATA_VERIFIER_COMMAND = 'test/verify-package-dependencies.sh "${packages[0]}"'
PUSH_STAGING_IMAGES_STEP = "Push verified staging images"
CREATE_MANIFESTS_STEP = "Create multi-architecture release manifests"
PUBLIC_ACCESS_STEP = "Verify public GHCR access"
PUBLIC_ACCESS_COMMAND = 'test/verify-ghcr-public-access.sh "${IMAGE_PREFIX}:latest"'
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


def disables_errexit?(command)
  options = command.split
  return false unless options.shift == "set"

  until options.empty?
    option = options.shift
    return false if option == "--" || (!option.start_with?("+") && !option.start_with?("-"))

    if option == "+o"
      return true if options.first == "errexit"

      options.shift
    elsif option.start_with?("+") && option.delete_prefix("+").include?("e")
      return true
    end
  end

  false
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


def assert_build_matrix(actual, expected, label)
  actual_include = actual.is_a?(Hash) ? actual["include"] : nil
  fail("#{label} is #{actual.inspect}, expected include matrix #{expected.inspect}") unless actual_include == expected
end


def assert_single_package_enumeration(command, label, verifier_command)
  lines = command.lines.map(&:strip).reject(&:empty?)
  nullglob_index = line_index(lines, "shopt -s nullglob", label)
  rpm_index = line_index(lines, "centos*) packages=(binaries/*.rpm) ;;", label)
  deb_index = line_index(lines, "*) packages=(binaries/*.deb) ;;", label)
  guard_index = line_index(lines, "if [[ ${#packages[@]} -ne 1 ]]; then", label)
  invocation_index = line_index(lines, verifier_command, label)
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


def assert_package_metadata(steps, label)
  metadata_index = step_index(steps, "Inspect package metadata")
  metadata_command = steps[metadata_index].fetch("run", "")
  assert_single_package_enumeration(metadata_command, "#{label} metadata",
                                    PACKAGE_METADATA_VERIFIER_COMMAND)
  metadata_index
end


def assert_release_package_contract(release)
  job = package_job(release, "release")
  assert_build_matrix(job.fetch("strategy").fetch("matrix"), EXPECTED_RELEASE_BUILD_MATRIX,
                      "release package matrix")
  fail("release package job must select its native runner from the matrix") unless job["runs-on"] == "${{ matrix.runner }}"
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
  assert_single_package_enumeration(verify_command, "release package verification",
                                    PACKAGE_VERIFIER_COMMAND)

  metadata_index = assert_package_metadata(package_steps, "release package")

  list_index = step_index(package_steps, "List built artifacts")
  upload_artifact_index = step_index(package_steps, "Upload package artifact")
  upload_release_index = step_index(package_steps, "Upload package to GitHub release")
  unless package_indexes.first < verify_index && verify_index < metadata_index && metadata_index < list_index && metadata_index < upload_artifact_index && metadata_index < upload_release_index
    fail("release package contents and metadata must be verified after packaging and before artifact handling")
  end

  artifact = package_steps[upload_artifact_index]
  fail("release package artifacts must retain their architecture") unless artifact["with"].fetch("name") == "package-${{ matrix.distro }}-${{ matrix.arch }}"
end


def assert_release_container_contract(release)
  job = release.fetch("jobs").fetch("container-build")
  assert_build_matrix(job.fetch("strategy").fetch("matrix"), EXPECTED_RELEASE_BUILD_MATRIX,
                      "release container-build matrix")
  fail("release container-build job must select its native runner from the matrix") unless job["runs-on"] == "${{ matrix.runner }}"
  container_steps = steps(job, "release container-build")
  download = container_steps.fetch(step_index(container_steps, "Download package"))
  fail("release container build must download the matching architecture package") unless download["with"].fetch("name") == "package-${{ matrix.distro }}-${{ matrix.arch }}"

  stage_index = step_index(container_steps, "Create architecture staging tag")
  stage_command = container_steps[stage_index].fetch("run", "")
  unless stage_command.include?("staging-${{ github.event.release.tag_name }}-${{ matrix.distro }}-${{ matrix.arch }}")
    fail("release container build must create a unique architecture staging tag")
  end

  remove_index = step_index(container_steps, "Remove non-staging tags")
  remove_command = container_steps[remove_index].fetch("run", "")
  unless remove_command.include?("docker image ls --format") &&
         remove_command.include?("$STAGING_IMAGE") &&
         remove_command.include?("docker image rm")
    fail("release container build must remove ordinary aliases before saving the staging artifact")
  end

  save_index = step_index(container_steps, "Save staged image to tarball")
  save_command = container_steps[save_index].fetch("run", "")
  fail("release container build must save only the architecture staging tag") unless save_command.include?("docker save \"$STAGING_IMAGE\"")
  unless stage_index < remove_index && remove_index < save_index
    fail("release container build must remove ordinary aliases before saving the staging artifact")
  end
  artifact = container_steps.fetch(step_index(container_steps, "Upload image artifact"))
  fail("release image artifacts must retain their architecture") unless artifact["with"].fetch("name") == "image-${{ matrix.distro }}-${{ matrix.arch }}"
end


def assert_release_publish_contract(release)
  publish_steps = steps(release.fetch("jobs").fetch("publish"), "release publish")
  buildx_index = step_index(publish_steps, "Set up Docker Buildx")
  push_index = step_index(publish_steps, PUSH_STAGING_IMAGES_STEP)
  manifest_index = step_index(publish_steps, CREATE_MANIFESTS_STEP)
  manifest_command = publish_steps[manifest_index].fetch("run", "")
  fail("release publish must compose manifests with docker buildx imagetools") unless manifest_command.include?("docker buildx imagetools create")
  unless manifest_command.include?("staging-${RELEASE_TAG}-${distro}-${arch}") &&
         manifest_command.include?("$(staging_image \"$distro\" amd64)") &&
         manifest_command.include?("$(staging_image \"$distro\" arm64)")
    fail("release publish must compose manifests from architecture staging images")
  end
  %w[debian13 ubuntu24].each do |distro|
    unless manifest_command.include?("publish_multiarch #{distro}")
      fail("release publish must compose #{distro} from native AMD64 and ARM64 staging images")
    end
  end
  fail("release publish must retain the Debian latest alias") unless manifest_command.include?("latest")
  fail("release publish must retain the Debian family alias") unless manifest_command.include?("debian")
  fail("release publish must retain the Ubuntu family alias") unless manifest_command.include?("ubuntu")
  public_access_index = step_index(publish_steps, PUBLIC_ACCESS_STEP)
  public_access_command = publish_steps[public_access_index].fetch("run", "")
  fail("release public-access verification must run #{PUBLIC_ACCESS_COMMAND.inspect}") unless public_access_command == PUBLIC_ACCESS_COMMAND
  unless buildx_index < push_index && push_index < manifest_index && manifest_index < public_access_index
    fail("release publish must push staging images, create manifests, then verify public access")
  end
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

  tag_index = step_index(test_steps, "Restore AMD64 distro tags for TAP")
  tag_command = test_steps[tag_index].fetch("run", "")
  unless tag_command.include?("for distro in #{EXPECTED_DISTROS.join(" ")}; do") &&
         tag_command.include?("staging-${{ github.event.release.tag_name }}-${distro}-amd64") &&
         tag_command.include?("${IMAGE_PREFIX}:${distro}")
    fail("release TAP tests must restore every AMD64 distro image tag")
  end
  fail("release TAP tests must restore image tags before compiling the TAP suite") unless tag_index < tap_index
rescue KeyError => error
  fail("release test-images job structure is incomplete: #{error.message}")
end


def assert_release_arm64_smoke_contract(release)
  job = release.fetch("jobs").fetch("arm64-smoke")
  fail("release ARM64 smoke job must use the native ARM64 runner") unless job["runs-on"] == ARM64_RUNNER
  fail("release ARM64 smoke job must depend on container builds") unless job["needs"] == "container-build"
  fail("release ARM64 smoke matrix must cover only current Debian and Ubuntu images") unless matrix(job, "distro", "release ARM64 smoke") == %w[debian13 ubuntu24]
  smoke_steps = steps(job, "release ARM64 smoke")
  download = smoke_steps.fetch(step_index(smoke_steps, "Download ARM64 image"))
  fail("release ARM64 smoke must download the matching ARM64 image artifact") unless download["with"].fetch("name") == "image-${{ matrix.distro }}-arm64"
  smoke = smoke_steps.fetch(step_index(smoke_steps, "Smoke test ARM64 runtime image"))
  smoke_command = smoke.fetch("run", "")
  unless smoke_command.include?("dpkg --print-architecture") && smoke_command.include?("arm64") && smoke_command.include?("proxysql_binlog_reader -v")
    fail("release ARM64 smoke must verify the native package architecture and reader executable")
  end
end


def assert_mysql_test_step(step, name, version)
  fail("#{name} must continue unless the workflow is cancelled") unless step["if"] == "${{ !cancelled() }}"
  fail("#{name} must time out after 30 minutes") unless step["timeout-minutes"] == 30
  fail("#{name} must not use continue-on-error") if step.key?("continue-on-error")

  environment = step["env"]
  mysql_versions = environment.is_a?(Hash) ? environment.fetch("MYSQL_VERSIONS", "").to_s : ""
  fail("#{name} must set MYSQL_VERSIONS to only #{version}") unless mysql_versions == version

  commands = shell_commands(step.fetch("run", ""))
  set_e_index = line_index(commands, "set -e", "#{name} commands")
  cleanup_index = line_index(commands, "cleanup() { docker compose down -v 2>/dev/null; }", "#{name} commands")
  trap_index = line_index(commands, "trap cleanup EXIT", "#{name} commands")
  mysql_index = line_index(commands, "docker compose up -d mysql", "#{name} commands")
  runner_index = line_index(commands, "docker compose run --rm runner", "#{name} commands")

  unless set_e_index < cleanup_index && cleanup_index < trap_index && trap_index < mysql_index && mysql_index < runner_index
    fail("#{name} must set up failure handling before starting and running MySQL")
  end
  fail("#{name} must keep errexit enabled until the TAP runner exits") if commands[set_e_index..runner_index].any? { |command| disables_errexit?(command) }
  fail("#{name} must leave the TAP runner as its final substantive command") unless runner_index == commands.length - 1
end


def assert_ci_contract(ci)
  fail("CI workflow permissions must be exactly contents: read") unless ci["permissions"] == {"contents" => "read"}

  jobs = ci.fetch("jobs")
  packages_job = jobs.fetch("packages")
  assert_build_matrix(packages_job.fetch("strategy").fetch("matrix"), EXPECTED_CI_PACKAGE_MATRIX,
                      "CI packages matrix")
  fail("CI packages must select the native runner from the matrix") unless packages_job["runs-on"] == "${{ matrix.runner }}"
  package_steps = steps(packages_job, "CI packages")
  package_verify_index = step_index(package_steps, "Verify package contents")
  assert_single_package_enumeration(package_steps[package_verify_index].fetch("run", ""), "CI package verification",
                                    PACKAGE_VERIFIER_COMMAND)
  metadata_index = assert_package_metadata(package_steps, "CI package")
  fail("CI package metadata must be checked after package contents") unless package_verify_index < metadata_index

  contract_job = jobs.fetch("workflow-contract")
  contract_steps = steps(contract_job, "workflow-contract")
  checkout_index = step_index(contract_steps, "Check out source")
  release_workflow_index = step_index(contract_steps, "Verify release workflow contract")
  ghcr_access_checker_index = step_index(contract_steps, GHCR_ACCESS_CHECKER_STEP)
  ghcr_access_checker_command = contract_steps[ghcr_access_checker_index].fetch("run", "")
  unless ghcr_access_checker_command == GHCR_ACCESS_CHECKER_COMMAND
    fail("workflow-contract GHCR access checker must run #{GHCR_ACCESS_CHECKER_COMMAND.inspect}")
  end
  unless checkout_index < release_workflow_index && release_workflow_index < ghcr_access_checker_index
    fail("workflow-contract must check out source and verify the release workflow before checking GHCR access")
  end

  runtime_image_configuration_index = step_index(contract_steps, RUNTIME_IMAGE_CONFIGURATION_STEP)
  runtime_image_configuration_command = contract_steps[runtime_image_configuration_index].fetch("run", "")
  unless runtime_image_configuration_command == RUNTIME_IMAGE_CONFIGURATION_COMMAND
    fail("workflow-contract runtime image configuration verification must run #{RUNTIME_IMAGE_CONFIGURATION_COMMAND.inspect}")
  end
  unless checkout_index < runtime_image_configuration_index && ghcr_access_checker_index < runtime_image_configuration_index
    fail("workflow-contract must check out source and verify GHCR access before verifying runtime image configuration")
  end

  libdaemon_arm64_configuration_index = step_index(contract_steps, LIBDAEMON_ARM64_CONFIGURATION_STEP)
  libdaemon_arm64_configuration_command = contract_steps[libdaemon_arm64_configuration_index].fetch("run", "")
  unless libdaemon_arm64_configuration_command == LIBDAEMON_ARM64_CONFIGURATION_COMMAND
    fail("workflow-contract libdaemon ARM64 bootstrap verification must run #{LIBDAEMON_ARM64_CONFIGURATION_COMMAND.inspect}")
  end
  unless runtime_image_configuration_index < libdaemon_arm64_configuration_index
    fail("workflow-contract must verify runtime image configuration before the libdaemon ARM64 bootstrap")
  end

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
  reader_build_index = step_index(test_steps, "Build and inspect vendored reader")
  reader_build_command = test_steps[reader_build_index].fetch("run", "")
  unless reader_build_command.include?("test/verify-shutdown-handshake.sh")
    fail("CI reader build must verify the shutdown handshake ordering")
  end

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
assert_release_arm64_smoke_contract(release)
assert_release_publish_contract(release)
assert_ci_contract(ci)
