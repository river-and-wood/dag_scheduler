workflow failure-demo
fail_fast false

task first
  cmd: echo first ok
end

task unstable
  deps: first
  retries: 1
  cmd: exit 1
end

task downstream
  deps: unstable
  cmd: echo should not run
end
