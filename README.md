libvmod-queryfilter
===================

> **Note:** This is an actively maintained community fork of the original [nytimes/libvmod-queryfilter](https://github.com/nytimes/libvmod-queryfilter). The original project by The New York Times Company is no longer maintained. All new development, bug fixes, and Varnish compatibility updates are managed here by [Petit Press](https://github.com/petitpress/libvmod-queryfilter).

Overview
--------
This is a simple VMOD for [Varnish Cache](https://www.varnish-cache.org/) which
provides query string filtering. It accepts a whitelist of parameter names and
returns the request URL with only the provided parameters and their values.

### Sort and Filter Functionality
The order of query string parameters is typically not considered, so an
effective caching strategy involves sorting the querystring parameters so that
identical name/value pairs that are in a different order for a given URL do not
create multiple cache records.

Libvmod-queryfilter does not require separate sort and filter calls because it
works by traversing the provided parameters and filtering as it goes.
The output contains parameter names in the same order that they were provided
to the VMOD, so by passing in parameter names in a consistent manner (e.g.,
alphabetical order), a resulting filtered request URL will be unique for its
combination of parameter names and values.

LICENSE
-------
Copyright © 2026 Petit Press, a.s.
Copyright © 2014-2020 The New York Times Company.
Licensed under the Apache 2.0 License. See [LICENSE](./LICENSE) for more information.

See the [NOTICE](./NOTICE) file for a list of contributors.

(To list individual developers, try `git shortlog -s` or [this page](https://github.com/petitpress/libvmod-queryfilter/graphs/contributors)).

Usage
-----
Rewrite the request URL so that the query string only contains parameter
name/value pairs for the "id" and "q" parameters:

##### Query Arrays Disabled

```
import queryfilter;
set req.url = queryfilter.filterparams(req.url, "id,q", false);
```

##### Query Arrays Enabled

```
import queryfilter;
set req.url = queryfilter.filterparams(req.url, "id,q,vals[]", true);
```

### Examples

#### Basic Parameter Filtering
```
# Input:  /path?a=1&b=2&c=3&d=4
# Filter: "a,c"
# Output: /path?a=1&c=3
```

#### Traditional Array Notation
```
# Input:  /path?item[]=first&item[]=second&other=remove
# Filter: "item[]" (with arrays enabled)
# Output: /path?item[]=first&item[]=second
```

#### Indexed Array Notation
```
# Input:  /path?item[0]=first&item[1]=second&other=remove
# Filter: "item[]" (with arrays enabled)
# Output: /path?item[0]=first&item[1]=second
```

#### Mixed Array Notations
```
# Input:  /path?item[]=first&item[0]=second&item[]=third
# Filter: "item[]" (with arrays enabled)
# Output: /path?item[]=first&item[0]=second&item[]=third
```

#### URL-Encoded Parameters
```
# Input:  /path?param=hello%20world&other=test%2Bvalue
# Filter: "param"
# Output: /path?param=hello%20world
# Note: percent-encoding is preserved verbatim in the output URI
```

#### URL-Encoded Array Brackets
```
# Input:  /path?items%5B0%5D=first&items%5B1%5D=second
# Filter: "items[]" (with arrays enabled)
# Output: /path?items%5B0%5D=first&items%5B1%5D=second
# Note: %5B = '[', %5D = ']'; decoding is used only for name matching,
#       the original encoding is kept in the output URI
```

#### Query Arrays
When query arrays are disabled, libvmod-queryfilter assumes query parameters are
individual name/value pairs (e.g. `a=1&b=2...`). Support for arrays in query
parameters - e.g. `a[]=1&a[]=2...` or `a[0]=1&a[1]=2...` - can be enabled by passing `true` for the `arrays_enabled`
argument. When this option is enabled, array parameters will be
preserved - in order - in the output URI.

The module supports both traditional array notation (`item[]=value`) and
indexed array notation (`item[0]=value`, `item[1]=value`, etc.). Both notations
can be mixed in the same query string and will be properly filtered when arrays
are enabled.

#### URL Decoding
The module decodes URL-encoded parameter *names* before comparing them against
the filter list. This includes both percent-encoding (e.g., `%5B` → `[`) and
plus-encoding (e.g., `+` → space). The decoded name is used **only for
matching**; the original percent-encoded form is preserved verbatim in the
output URI.

**What this means in practice:**
- `items%5B0%5D=v` is matched by a filter that specifies `items[]` (arrays enabled)
- The output still contains `items%5B0%5D=v` - nothing is re-encoded or decoded
- Parameter values are never decoded; they are always copied as-is
- Supported encodings: `%XX` (any hex pair), `+` (space in names)

Building
--------
Libvmod-queryfilter attempts to be 100% C99 conformant. You should, therefore,
be able to build it without issue on most major compilers. The vmod can be
built against a compiled varnish source, or against an installed
`varnish-dev/-devel` package which includes the appropriate `.pc` files.

### Setup
Before anything else is done, your source directory has to be initialized:

```sh
./autogen.sh
```

### Configuration
To build against a standard varnish development package, you should be able to
simply invoke:
```sh
./configure && make && make check
```
(See `./configure --help` for configure-time options)

This vmod can also be compiled against a pre-built Varnish Cache 3.x/4.x/5.x/6.x/7.x/8.x
source by indicating the path to the (pre-compiled!) varnish source using the
`VARNISHSRC` configuration variable, like so:

```sh
./configure VARNISHSRC=path/to/varnish-M.m.p && make && make check
```

Additional configuration variables and options can be found by invoking
`configure --help`.

### Check Targets
Libvmod-queryfilter provides a set of simple unit tests driven by
**varnishtest**. They can be executed as part of the build process by
invoking `make check`.
