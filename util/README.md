# Util

 - `run_tests.sh`: Wrapper script used by the makefile to invoke varnishtest against the VCL files in this repo.
 - `docker-tests.sh`: Build and test the VMOD against multiple Varnish versions using Docker.

   Usage:
   ```bash
   # Use default versions (6.0.17, 7.6.5, 7.7.3, 8.0.1)
   ./util/docker-tests.sh

   # Test single version
   VARNISH_VERSIONS=8.0.1 ./util/docker-tests.sh

   # Test multiple versions (space-separated string)
   VARNISH_VERSIONS="6.0.17 8.0.1" ./util/docker-tests.sh

   # Using export also works
   export VARNISH_VERSIONS="6.0.17 8.0.1"
   ./util/docker-tests.sh
   ```
