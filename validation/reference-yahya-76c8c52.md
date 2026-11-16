# Yahya reference snapshot

Source: https://github.com/YahyaOukharta/cultist-wars
Commit: 76c8c52

Clean external checkout preserved as a file snapshot, without nested Git metadata.
Restore from repository root:

```sh
tar -xzf validation/reference-yahya-76c8c52.tar.gz
```

Large experiment files (10 MB or larger) remain local, excluded by exact paths in `.gitignore`. Auth tokens and generated build files remain excluded.
