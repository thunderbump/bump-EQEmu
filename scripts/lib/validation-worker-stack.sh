#!/usr/bin/env bash

validation_worker_lock_path() {
  local stack_dir="$1" lock_root lock_key lock_name

  lock_root="${TMPDIR:-/tmp}/bump-eqemu-validation-worker-locks"
  if ! mkdir -p "$lock_root"; then
    return 1
  fi
  if ! lock_key="$(printf '%s' "$stack_dir" | cksum | awk '{print $1}')"; then
    return 1
  fi
  lock_name="$(basename "$stack_dir")"
  lock_name="${lock_name//[^[:alnum:]_.-]/_}"
  printf '%s/%s-%s.lock\n' "$lock_root" "$lock_name" "$lock_key"
}

validation_worker_acquire_stack_lock() {
  local stack_dir="$1" lock_wait_seconds="$2" worker_lock

  if ! worker_lock="$(validation_worker_lock_path "$stack_dir")"; then
    printf 'error: unable to prepare validation worker lock for stack: %s\n' "$stack_dir" >&2
    return 75
  fi
  if ! { exec 9>"$worker_lock"; }; then
    printf 'error: unable to open validation worker lock: %s\n' "$worker_lock" >&2
    return 75
  fi
  if ! flock -w "$lock_wait_seconds" 9; then
    printf 'error: timed out waiting for validation worker lock: %s\n' "$worker_lock" >&2
    return 75
  fi
}

validation_code_path=""
validation_code_restore_mode="none"
validation_code_restore_target=""

validation_worker_restore_stack_code() {
  local mode="$validation_code_restore_mode"

  [[ "$mode" != "none" ]] || return 0
  validation_code_restore_mode="none"

  case "$mode" in
    symlink)
      if [[ -L "$validation_code_path" || ! -e "$validation_code_path" ]]; then
        rm -f -- "$validation_code_path"
        ln -s "$validation_code_restore_target" "$validation_code_path"
      else
        printf 'error: cannot restore validation stack code symlink; path exists and is not a symlink: %s\n' "$validation_code_path" >&2
        return 1
      fi
      ;;
    absent)
      if [[ -L "$validation_code_path" || ! -e "$validation_code_path" ]]; then
        rm -f -- "$validation_code_path"
      else
        printf 'error: cannot restore absent validation stack code path; path exists and is not a symlink: %s\n' "$validation_code_path" >&2
        return 1
      fi
      ;;
  esac
}

validation_worker_cleanup() {
  local status=$?

  trap - EXIT
  if ! validation_worker_restore_stack_code; then
    status=1
  fi
  exit "$status"
}

validation_worker_bind_stack_code() {
  local code_path="$AKKSTACK_STACK_DIR/code" resolved_code old_target

  validation_code_path="$code_path"

  if [[ -e "$code_path" ]]; then
    resolved_code="$(akkstack_resolve_path "$code_path")"
    if [[ "$resolved_code" == "$repo_root" ]]; then
      record_step checkout_binding pass ok "validation stack code already points at requested checkout" \
        "resolved code: $resolved_code"
      return 0
    fi

    if [[ ! -L "$code_path" ]]; then
      record_step checkout_binding fail unsafe_code_path "validation stack code path exists and is not a symlink" \
        "code path: $code_path" "resolved code: $resolved_code" "expected: $repo_root"
      return 1
    fi
  elif [[ -L "$code_path" ]]; then
    resolved_code="$(akkstack_resolve_path "$code_path")"
  else
    if [[ ! -d "$AKKSTACK_STACK_DIR" ]]; then
      record_step checkout_binding fail missing_validation_stack "validation stack directory is missing; cannot bind code" \
        "stack path: $AKKSTACK_STACK_DIR"
      return 1
    fi
    validation_code_restore_mode="absent"
    validation_code_restore_target=""
    trap validation_worker_cleanup EXIT
    if ! ln -s "$repo_root" "$code_path"; then
      record_step checkout_binding fail unsafe_code_path "failed to create validation stack code symlink" \
        "code path: $code_path" "expected: $repo_root"
      return 1
    fi
    record_step checkout_binding pass ok "validation stack code bound to requested checkout" \
      "code path: $code_path" "resolved code: $repo_root"
    return 0
  fi

  old_target="$(readlink "$code_path")"
  validation_code_restore_mode="symlink"
  validation_code_restore_target="$old_target"
  trap validation_worker_cleanup EXIT
  if ! rm -f -- "$code_path"; then
    record_step checkout_binding fail unsafe_code_path "failed to replace validation stack code symlink" \
      "code path: $code_path" "previous target: $old_target" "expected: $repo_root"
    return 1
  fi
  if ! ln -s "$repo_root" "$code_path"; then
    record_step checkout_binding fail unsafe_code_path "failed to bind validation stack code symlink" \
      "code path: $code_path" "previous target: $old_target" "expected: $repo_root"
    return 1
  fi

  record_step checkout_binding pass ok "validation stack code rebound to requested checkout" \
    "code path: $code_path" "previous target: $old_target" "resolved code: $repo_root"
}
