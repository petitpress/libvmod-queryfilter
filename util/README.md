# Util

 - `run_tests.sh`: Wrapper script used by the makefile to invoke varnishtest against the VCL files in this repo.
 - `docker-tests.sh`: Build and test the VMOD against multiple Varnish versions using Docker.

   Usage:
   ```bash
   # Use default versions (6.0.14, 7.6.3, 7.7.1)
   ./util/docker-tests.sh

   # Test single version
   VARNISH_VERSIONS=7.6.3 ./util/docker-tests.sh

   # Test multiple versions (space-separated string)
   VARNISH_VERSIONS="7.6.2 7.7.0" ./util/docker-tests.sh

   # Using export also works
   export VARNISH_VERSIONS="7.6.3 7.7.1"
   ./util/docker-tests.sh
   ```
