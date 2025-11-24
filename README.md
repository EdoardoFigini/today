# Today

Small CLI calendar client application.


## Installation 

Clone this repository and compile from source.

### Requirements

On Linux `today` depends on [openssl](https://github.com/openssl/openssl).

### Compilation

This application uses the [nob](https://github.com/tsoding/nob.h) build system.

1. Bootstrap nob 

    Windows:

    ```console
    > cl nob.c /Fe:nob.exe
    ```

    Linux:

    ```console
    $ gcc nob.c -o nob
    ```

2. Build

    ```console
    $ nob
    ```

## Usage

Once compiled, the application is in `/path/to/today/bin/`.

The first time you run `today` you will need to add a URL to sync.

```console
$ today -a https://url.to/calendar
```

After adding your url you can see the events by running

```console
$ today
```

or 

```console
$ today table
```

### Arguments

1. Format arguments
    
    - `list` prints the events in list form
    
    - `table` prints the events in table form

2. Flags

    - `help`: shows the usage message.

    - `add <url>`: adds a url to sync to. (You probably would need this flag the first time you run `today`)

    - `delete <url>`: deletes a url from the saved ones.

    - `refresh`: refreshes the calendars using the urls saved with the `add` flag
