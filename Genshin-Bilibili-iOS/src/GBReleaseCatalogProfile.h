/* Real values stay outside Git. Unit tests use an explicitly synthetic profile. */
#ifdef GB_CATALOG_TEST_PROFILE
#include "../tests/fixtures/catalog_profile.h"
#else
#include "../../private/GBReleaseCatalogProfile.h"
#endif
