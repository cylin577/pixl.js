# generate i18n sources from fw/data/i18n.csv
#
# Only the languages listed in fw/data/i18n_languages.txt are generated and
# compiled into the firmware. Every other column of i18n.csv is ignored and its
# generated .c file (if any) is removed.

import csv
import os


class I18nFile:
    def __init__(self):
        self.avaliable_languages = list()
        self.i18n_items = list()


def get_prorject_directory():
    return os.path.abspath(os.path.dirname(__file__) + "/../")


def get_i18n_src_directory():
    return get_prorject_directory() + "/application/src/i18n"


def read_i18n_from_csv():
    i18n = I18nFile()
    csv_file = get_prorject_directory() + "/data/i18n.csv"
    if not os.path.exists(csv_file):
        return i18n

    with open(csv_file, "r", encoding="utf8") as f:
        first_line = True
        for r in csv.reader(f):
            if first_line:
                first_line = False
                i18n.avaliable_languages = r[1:]
            else:
                i18n.i18n_items.append(r)
    return i18n


def read_enabled_languages(avaliable_languages):
    """Read fw/data/i18n_languages.txt -> list of (csv_name, description)."""
    config_file = get_prorject_directory() + "/data/i18n_languages.txt"
    if not os.path.exists(config_file):
        # No config: compile every language available in the CSV.
        return [(lang, lang) for lang in avaliable_languages]

    enabled = list()
    with open(config_file, "r", encoding="utf8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            name, _, desc = line.partition(",")
            name = name.strip()
            desc = desc.strip()
            if name not in avaliable_languages:
                raise ValueError("%s: unknown language '%s' (not a column of i18n.csv)" % (config_file, name))
            enabled.append((name, desc))

    if not enabled:
        raise ValueError("%s: no languages selected" % config_file)
    if "en_US" not in [name for name, _ in enabled]:
        # getLangString() falls back to lang_en_US, so it must always exist.
        raise ValueError("%s: en_US must always be enabled (fallback language)" % config_file)
    return enabled


def language_to_enum_suffix(name):
    return name.upper()


def write_i18n_string_id_file(i18n):
    c_file = get_i18n_src_directory() + "/string_id.h"
    with open(c_file, "w+", newline="\n", encoding="utf8") as f:
        f.write('#ifndef LANGUAGE_STRING_ID_H\n')
        f.write('#define LANGUAGE_STRING_ID_H\n')
        f.write('typedef enum {\n')
        for item in i18n.i18n_items:
            f.write('    ' + item[0] + ',\n')
        f.write('    _L_COUNT,\n')
        f.write('} L_StringID;\n')
        f.write('#endif\n')


def write_language_list_file(enabled):
    """Generate language_list.h: an X-macro describing the compiled languages."""
    c_file = get_i18n_src_directory() + "/language_list.h"
    with open(c_file, "w+", newline="\n", encoding="utf8") as f:
        f.write('#ifndef LANGUAGE_LIST_H\n')
        f.write('#define LANGUAGE_LIST_H\n\n')
        f.write('// Generated from fw/data/i18n_languages.txt - do not edit.\n')
        f.write('// X(enum_suffix, locale_c_name, native_description)\n')
        f.write('#define LANGUAGE_LIST(X) \\\n')
        for i, (name, desc) in enumerate(enabled):
            sep = ' \\' if i < len(enabled) - 1 else ''
            f.write('    X(%s, %s, "%s")%s\n' % (language_to_enum_suffix(name), name, desc, sep))
        f.write('\n#endif\n')


def write_i18n_language_c(i18n, idx, lang):
    c_file = get_i18n_src_directory() + "/" + lang + ".c"
    with open(c_file, "w+", newline="\n", encoding="utf8") as f:
        f.write('#include "string_id.h"\n')
        f.write('const char * const lang_' + lang + '[_L_COUNT] = {\n')
        for item in i18n.i18n_items:
            f.write('    [%s] = "%s",\n' % (item[0], item[idx]))
        f.write('};\n')


def remove_stale_language_c(avaliable_languages, enabled):
    keep = set(name for name, _ in enabled) | {"language"}
    for lang in avaliable_languages:
        if lang not in keep:
            c_file = get_i18n_src_directory() + "/" + lang + ".c"
            if os.path.exists(c_file):
                os.remove(c_file)
                print("removed stale " + os.path.basename(c_file))


i18n_file = read_i18n_from_csv()
enabled_languages = read_enabled_languages(i18n_file.avaliable_languages)

write_i18n_string_id_file(i18n_file)
write_language_list_file(enabled_languages)

for lang, _ in enabled_languages:
    idx = i18n_file.avaliable_languages.index(lang) + 1
    write_i18n_language_c(i18n_file, idx, lang)

remove_stale_language_c(i18n_file.avaliable_languages, enabled_languages)
