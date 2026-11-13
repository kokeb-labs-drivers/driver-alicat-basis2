# BASIS 2 Driver API reference {#mainpage}

The classes, types and registers of the BASIS 2 driver, generated from the headers in `inc/`.

Start with alicat_basis2::Basis2, one instrument, and alicat_basis2::Bus, the serial line it sits on. The base of the
port you write is alicat_basis2::UartInterface. Errors are alicat_basis2::Error inside alicat_basis2::Result; the
configuration is alicat_basis2::Config.

The guides, with examples that compile, are on the documentation site: <a href="../">BASIS 2 Driver documentation</a>.
