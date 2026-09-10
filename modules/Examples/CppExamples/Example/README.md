# Open Telemetry Demo

For now only bazel - to run it 
```sh
docker run --rm -p 3000:3000 -p 4318:4318 grafana/otel-lgtm
OTEL_METRIC_EXPORT_INTERVAL=5000 bazel run //modules/Examples/CppExamples/Example:otel_env
```