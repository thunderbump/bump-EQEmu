#!/usr/bin/env bash

akkstack_parse_error() {
  printf 'error: %s\n' "$*" >&2
  exit 2
}

akkstack_die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

akkstack_resolve_path() {
  local path="$1"

  if command -v realpath >/dev/null 2>&1; then
    realpath -m "$path"
  elif command -v readlink >/dev/null 2>&1 && [[ -e "$path" ]]; then
    readlink -f "$path"
  elif [[ "$path" == /* ]]; then
    printf '%s\n' "$path"
  else
    printf '%s/%s\n' "$PWD" "$path"
  fi
}

akkstack_validate_role() {
  local role="$1"

  case "$role" in
    validation|gameplay)
      ;;
    *)
      akkstack_parse_error "invalid --stack role '$role' (expected validation or gameplay)"
      ;;
  esac
}

akkstack_check_default_paths_distinct() {
  if [[ -e "$AKKSTACK_VALIDATION_DEFAULT_DIR" && -e "$AKKSTACK_GAMEPLAY_DEFAULT_DIR" ]] \
    && [[ "$AKKSTACK_VALIDATION_DEFAULT_RESOLVED" == "$AKKSTACK_GAMEPLAY_DEFAULT_RESOLVED" ]]; then
    akkstack_die "default validation and gameplay AkkStack paths must not resolve to the same directory: $AKKSTACK_VALIDATION_DEFAULT_RESOLVED"
  fi
}

akkstack_init_routing() {
  local repo_root="$1"
  local default_role="$2"
  shift 2

  AKKSTACK_REPO_ROOT="$repo_root"
  AKKSTACK_DEFAULT_ROLE="$default_role"
  AKKSTACK_VALIDATION_DEFAULT_DIR="$repo_root/../bump-akk-stack-validation"
  AKKSTACK_GAMEPLAY_DEFAULT_DIR="$repo_root/../bump-akk-stack"
  AKKSTACK_VALIDATION_DEFAULT_RESOLVED="$(akkstack_resolve_path "$AKKSTACK_VALIDATION_DEFAULT_DIR")"
  AKKSTACK_GAMEPLAY_DEFAULT_RESOLVED="$(akkstack_resolve_path "$AKKSTACK_GAMEPLAY_DEFAULT_DIR")"
  AKKSTACK_STACK_ROLE="$default_role"
  AKKSTACK_DRY_RUN=0
  AKKSTACK_HELP=0
  AKKSTACK_REMAINING_ARGS=()

  akkstack_validate_role "$default_role"

  while [[ "$#" -gt 0 ]]; do
    case "$1" in
      --stack)
        [[ "$#" -ge 2 ]] || akkstack_parse_error "--stack requires validation or gameplay"
        AKKSTACK_STACK_ROLE="$2"
        shift 2
        ;;
      --stack=*)
        AKKSTACK_STACK_ROLE="${1#--stack=}"
        shift
        ;;
      --dry-run)
        AKKSTACK_DRY_RUN=1
        shift
        ;;
      -h|--help|help)
        AKKSTACK_HELP=1
        AKKSTACK_REMAINING_ARGS+=("$1")
        shift
        ;;
      --)
        shift
        while [[ "$#" -gt 0 ]]; do
          AKKSTACK_REMAINING_ARGS+=("$1")
          shift
        done
        ;;
      -*)
        akkstack_parse_error "unknown option '$1'"
        ;;
      *)
        AKKSTACK_REMAINING_ARGS+=("$1")
        shift
        ;;
    esac
  done

  akkstack_validate_role "$AKKSTACK_STACK_ROLE"
  if [[ "$AKKSTACK_HELP" -eq 0 ]]; then
    akkstack_check_default_paths_distinct
  fi

  if [[ -v AKKSTACK_DIR ]]; then
    [[ -n "$AKKSTACK_DIR" ]] || akkstack_parse_error "AKKSTACK_DIR is set but empty"
    AKKSTACK_STACK_DIR_RAW="$AKKSTACK_DIR"
    AKKSTACK_PATH_SOURCE="AKKSTACK_DIR"
  else
    case "$AKKSTACK_STACK_ROLE" in
      validation)
        AKKSTACK_STACK_DIR_RAW="$AKKSTACK_VALIDATION_DEFAULT_DIR"
        AKKSTACK_PATH_SOURCE="validation default"
        ;;
      gameplay)
        AKKSTACK_STACK_DIR_RAW="$AKKSTACK_GAMEPLAY_DEFAULT_DIR"
        AKKSTACK_PATH_SOURCE="gameplay default"
        ;;
    esac
  fi

  AKKSTACK_STACK_DIR="$(akkstack_resolve_path "$AKKSTACK_STACK_DIR_RAW")"
}

akkstack_print_selection() {
  local title="${1:-AkkStack selection}"

  printf '%s\n' "$title"
  printf '  stack role: %s\n' "$AKKSTACK_STACK_ROLE"
  printf '  stack path: %s\n' "$AKKSTACK_STACK_DIR"
  printf '  path source: %s\n' "$AKKSTACK_PATH_SOURCE"
}

akkstack_print_compose_files() {
  printf '  compose files:\n'

  if [[ "$#" -eq 0 ]]; then
    printf '    - (none)\n'
    return
  fi

  local compose_file
  for compose_file in "$@"; do
    printf '    - %s\n' "$compose_file"
  done
}

akkstack_print_dry_run() {
  local action="$1"
  shift

  akkstack_print_selection "AkkStack dry run"
  akkstack_print_compose_files "$@"
  printf '  action: %s\n' "$action"
  printf 'Dry run: Docker was not invoked.\n'
}

akkstack_require_selected_stack_dir() {
  if [[ -d "$AKKSTACK_STACK_DIR" ]]; then
    return
  fi

  case "$AKKSTACK_PATH_SOURCE" in
    "validation default")
      akkstack_die "default validation AkkStack directory is missing: $AKKSTACK_STACK_DIR. Create ../bump-akk-stack-validation, set AKKSTACK_DIR=/path/to/custom-stack, or pass --stack gameplay only for an intentional gameplay target."
      ;;
    "gameplay default")
      akkstack_die "default gameplay AkkStack directory is missing: $AKKSTACK_STACK_DIR"
      ;;
    *)
      akkstack_die "AkkStack directory is missing: $AKKSTACK_STACK_DIR"
      ;;
  esac
}

akkstack_is_gameplay_target() {
  if [[ "$AKKSTACK_STACK_ROLE" == "gameplay" ]]; then
    return 0
  fi

  if [[ -e "$AKKSTACK_GAMEPLAY_DEFAULT_DIR" && "$AKKSTACK_STACK_DIR" == "$AKKSTACK_GAMEPLAY_DEFAULT_RESOLVED" ]]; then
    return 0
  fi

  return 1
}

akkstack_warn_if_validation_command_targets_gameplay() {
  local command_name="${1:-validation command}"

  if akkstack_is_gameplay_target; then
    printf 'WARNING: %s is targeting the gameplay AkkStack; validation may read or mutate persistent gameplay data.\n' "$command_name" >&2
  fi
}
