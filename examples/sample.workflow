# Basic successful DAG with fan-out/fan-in
workflow sample-build
fail_fast false

task prepare
  cmd: echo preparing
  priority: 10
end

task compile_a
  deps: prepare
  cmd: sleep 1; echo compile_a done
  timeout_ms: 5000
end

task compile_b
  deps: prepare
  cmd: sleep 1; echo compile_b done
  timeout_ms: 5000
end

task package
  deps: compile_a, compile_b
  cmd: echo package done
  retries: 1
  priority: 5
end
