#include <assert.h>
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "expat.h"


static XML_Parser parser;


static void
basic_setup(void)
{
    parser = XML_ParserCreate(NULL);
    if (parser == NULL)
        fail("Parser not created.");
}

static void
basic_teardown(void)
{
    if (parser != NULL)
        XML_ParserFree(parser);
}

/* Generate a failure using the parser state to create an error message;
 * this should be used when the parser reports an error we weren't
 * expecting.
 */
static void
_xml_failure(const char *file, int line)
{
    char buffer[1024];
    sprintf(buffer, "%s (line %d, offset %d)\n    reported from %s, line %d",
            XML_ErrorString(XML_GetErrorCode(parser)),
            XML_GetCurrentLineNumber(parser),
            XML_GetCurrentColumnNumber(parser),
            file, line);
    fail(buffer);
}

#define xml_failure() _xml_failure(__FILE__, __LINE__)

START_TEST(test_nul_byte)
{
    char text[] = "<doc>\0</doc>";

    /* test that a NUL byte (in US-ASCII data) is an error */
    if (XML_Parse(parser, text, sizeof(text) - 1, 1))
        fail("Parser did not report error on NUL-byte.");
    if (XML_GetErrorCode(parser) != XML_ERROR_INVALID_TOKEN)
        xml_failure();
}
END_TEST


START_TEST(test_u0000_char)
{
    char *text = "<doc>&#0;</doc>";

    /* test that a NUL byte (in US-ASCII data) is an error */
    if (XML_Parse(parser, text, strlen(text), 1))
        fail("Parser did not report error on NUL-byte.");
    if (XML_GetErrorCode(parser) != XML_ERROR_BAD_CHAR_REF)
        xml_failure();
}
END_TEST


START_TEST(test_xmldecl_misplaced)
{
    char *text =
        "\n"
        "<?xml version='1.0'?>\n"
        "<a>&eee;</a>";

    if (!XML_Parse(parser, text, strlen(text), 1)) {
        if (XML_GetErrorCode(parser) != XML_ERROR_MISPLACED_XML_PI)
            xml_failure();
    }
    else {
        fail("expected XML_ERROR_MISPLACED_XML_PI with misplaced XML decl");
    }
}
END_TEST

START_TEST(test_bom_utf8)
{
    /* This test is really just making sure we don't core on a UTF-8 BOM. */
    char *text = "\357\273\277<e/>";

    if (!XML_Parse(parser, text, strlen(text), 1))
        xml_failure();
}
END_TEST

START_TEST(test_bom_utf16_be)
{
    char text[] = "\376\377\0<\0e\0/\0>";

    if (!XML_Parse(parser, text, sizeof(text) - 1, 1))
        xml_failure();
}
END_TEST

START_TEST(test_bom_utf16_le)
{
    char text[] = "\377\376<\0e\0/\0>\0";

    if (!XML_Parse(parser, text, sizeof(text) - 1, 1))
        xml_failure();
}
END_TEST


typedef struct 
{
    int count;
    XML_Char data[1024];
} CharData;

static void
accumulate_characters(void *userData, const XML_Char *s, int len)
{
    CharData *storage = (CharData *)userData;
    if (len + storage->count < sizeof(storage->data)) {
        memcpy(storage->data + storage->count, s, len);
        storage->count += len;
    }
}

static void
check_characters(CharData *storage, XML_Char *expected)
{
    char buffer[1024];
    int len = strlen(expected);
    if (len != storage->count) {
        sprintf(buffer, "wrong number of data characters: got %d, expected %d",
                storage->count, len);
        fail(buffer);
        return;
    }
    if (memcmp(expected, storage->data, len) != 0)
        fail("got bad data bytes");
}

static void
run_character_check(XML_Char *text, XML_Char *expected)
{
    CharData storage;
    storage.count = 0;
    XML_SetUserData(parser, &storage);
    XML_SetCharacterDataHandler(parser, accumulate_characters);
    if (!XML_Parse(parser, text, strlen(text), 1))
        xml_failure();
    check_characters(&storage, expected);
}

/* Regression test for SF bug #491986. */
START_TEST(test_danish_latin1)
{
    char *text =
        "<?xml version='1.0' encoding='iso-8859-1'?>\n"
        "<e>J�rgen ������</e>";
    run_character_check(text,
             "J\xC3\xB8rgen \xC3\xA6\xC3\xB8\xC3\xA5\xC3\x86\xC3\x98\xC3\x85");
}
END_TEST
/* End regression test for SF bug #491986. */


/* Regression test for SF bug #514281. */
START_TEST(test_french_charref_hexidecimal)
{
    char *text =
        "<?xml version='1.0' encoding='iso-8859-1'?>\n"
        "<doc>&#xE9;&#xE8;&#xE0;&#xE7;&#xEA;&#xC8;</doc>";
    run_character_check(text,
                        "\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xAA\xC3\x88");
}
END_TEST

START_TEST(test_french_charref_decimal)
{
    char *text =
        "<?xml version='1.0' encoding='iso-8859-1'?>\n"
        "<doc>&#233;&#232;&#224;&#231;&#234;&#200;</doc>";
    run_character_check(text,
                        "\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xAA\xC3\x88");
}
END_TEST

START_TEST(test_french_latin1)
{
    char *text =
        "<?xml version='1.0' encoding='iso-8859-1'?>\n"
        "<doc>\xE9\xE8\xE0\xE7\xEa\xC8</doc>";
    run_character_check(text,
                        "\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xAA\xC3\x88");
}
END_TEST

START_TEST(test_french_utf8)
{
    char *text =
        "<?xml version='1.0' encoding='utf-8'?>\n"
        "<doc>\xC3\xA9</doc>";
    run_character_check(text, "\xC3\xA9");
}
END_TEST
/* End regression test for SF bug #514281. */

/* Regression test for SF bug #477667.
 * This test assures that any 8-bit character followed by a 7-bit
 * character will not be mistakenly interpreted as a valid UTF-8
 * sequence.
 */
START_TEST(test_illegal_utf8)
{
    char text[100];
    int i;

    for (i = 128; i <= 255; ++i) {
        sprintf(text, "<e>%ccd</e>", i);
        if (XML_Parse(parser, text, strlen(text), 1)) {
            sprintf(text,
                    "expected token error for '%c'(ordinal %d) in UTF-8 text",
                    i, i);
            fail(text);
        }
        else if (XML_GetErrorCode(parser) != XML_ERROR_INVALID_TOKEN)
            xml_failure();
    }
}
END_TEST

START_TEST(test_utf16)
{
    /* <?xml version="1.0" encoding="UTF-16"?>
     * <doc a='123'>some text</doc>
     */
    char text[] =
        "\000<\000?\000x\000m\000\154\000 \000v\000e\000r\000s\000i\000o"
        "\000n\000=\000'\0001\000.\000\060\000'\000 \000e\000n\000c\000o"
        "\000d\000i\000n\000g\000=\000'\000U\000T\000F\000-\0001\000\066"
        "\000'\000?\000>\000\n"
        "\000<\000d\000o\000c\000 \000a\000=\000'\0001\0002\0003\000'"
        "\000>\000s\000o\000m\000e\000 \000t\000e\000x\000t\000<\000/"
        "\000d\000o\000c\000>";
    if (!XML_Parse(parser, text, sizeof(text) - 1, 1))
        xml_failure();
}
END_TEST


/* Helpers used by the following test; this checks any "attr" and "refs"
 * attributes to make sure whitespace has been normalized.
 */

/* Return true if whitespace has been normalized in a string, using
 * the rules for attribute value normalization.  The 'is_cdata' flag
 * is needed since CDATA attributes don't need to have multiple
 * whitespace characters collapsed to a single space, while other
 * attribute data types do.  (Section 3.3.3 of the recommendation.)
 */
static int
is_whitespace_normalized(const XML_Char *s, int is_cdata)
{
    int blanks = 0;
    int at_start = 1;
    while (*s) {
        if (*s == ' ')
            ++blanks;
        else if (*s == '\t' || *s == '\n' || *s == '\r')
            return 0;
        else {
            if (at_start) {
                at_start = 0;
                if (blanks && !is_cdata)
                    /* illegal leading blanks */
                    return 0;
            }
            else if (blanks > 1 && !is_cdata)
                return 0;
            blanks = 0;
        }
        ++s;
    }
    if (blanks && !is_cdata)
        return 0;
    return 1;
}

/* Check the attribute whitespace checker: */
static void
testhelper_is_whitespace_normalized(void)
{
    assert(is_whitespace_normalized("abc", 0));
    assert(is_whitespace_normalized("abc", 1));
    assert(is_whitespace_normalized("abc def ghi", 0));
    assert(is_whitespace_normalized("abc def ghi", 1));
    assert(!is_whitespace_normalized(" abc def ghi", 0));
    assert(is_whitespace_normalized(" abc def ghi", 1));
    assert(!is_whitespace_normalized("abc  def ghi", 0));
    assert(is_whitespace_normalized("abc  def ghi", 1));
    assert(!is_whitespace_normalized("abc def ghi ", 0));
    assert(is_whitespace_normalized("abc def ghi ", 1));
    assert(!is_whitespace_normalized(" ", 0));
    assert(is_whitespace_normalized(" ", 1));
    assert(!is_whitespace_normalized("\t", 0));
    assert(!is_whitespace_normalized("\t", 1));
    assert(!is_whitespace_normalized("\n", 0));
    assert(!is_whitespace_normalized("\n", 1));
    assert(!is_whitespace_normalized("\r", 0));
    assert(!is_whitespace_normalized("\r", 1));
    assert(!is_whitespace_normalized("abc\t def", 1));
}

static void
check_attr_contains_normalized_whitespace(void *userdata,
                                          const XML_Char *name,
                                          const XML_Char **atts)
{
    int i;
    for (i = 0; atts[i] != NULL; i += 2) {
        const XML_Char *attrname = atts[i];
        const XML_Char *value = atts[i + 1];
        if (strcmp("attr", attrname) == 0
            || strcmp("ents", attrname) == 0
            || strcmp("refs", attrname) == 0) {
            if (!is_whitespace_normalized(value, 0)) {
                char buffer[256];
                sprintf(buffer, "attribute value not normalized: %s='%s'",
                        attrname, value);
                fail(buffer);
            }
        }
    }
}

START_TEST(test_attr_whitespace_normalization)
{
    char *text =
        "<!DOCTYPE doc [\n"
        "  <!ATTLIST doc\n"
        "            attr NMTOKENS #REQUIRED\n"
        "            ents ENTITIES #REQUIRED\n"
        "            refs IDREFS   #REQUIRED>\n"
        "]>\n"
        "<doc attr='    a  b c\t\td\te\t' refs=' id-1   \t  id-2\t\t'  \n"
        "     ents=' ent-1   \t\r\n"
        "            ent-2  ' >\n"
        "  <e id='id-1'/>\n"
        "  <e id='id-2'/>\n"
        "</doc>";

    XML_SetStartElementHandler(parser,
                               check_attr_contains_normalized_whitespace);
    if (!XML_Parse(parser, text, strlen(text), 1))
        xml_failure();
}
END_TEST


/*
 * Namespaces tests.
 */

static void
namespace_setup(void)
{
    parser = XML_ParserCreateNS(NULL, ' ');
    if (parser == NULL)
        fail("Parser not created.");
}

static void
namespace_teardown(void)
{
    basic_teardown();
}

/* Check that an element name and attribute name match the expected values.
 * The expected values are passed as an array reference of string pointers
 * provided as the userData argument; the first is the expected
 * element name, and the second is the expected attribute name.
 */
static void
triplet_start_checker(void *userData, const XML_Char *name,
                      const XML_Char **atts)
{
    char **elemstr = (char **)userData;
    char buffer[1024];
    if (strcmp(elemstr[0], name) != 0) {
        sprintf(buffer, "unexpected start string: '%s'", name);
        fail(buffer);
    }
    if (strcmp(elemstr[1], atts[0]) != 0) {
        sprintf(buffer, "unexpected attribute string: '%s'", atts[0]);
        fail(buffer);
    }
}

/* Check that the element name passed to the end-element handler matches
 * the expected value.  The expected value is passed as the first element
 * in an array of strings passed as the userData argument.
 */
static void
triplet_end_checker(void *userData, const XML_Char *name)
{
    char **elemstr = (char **)userData;
    if (strcmp(elemstr[0], name) != 0) {
        char buffer[1024];
        sprintf(buffer, "unexpected end string: '%s'", name);
        fail(buffer);
    }
}

START_TEST(test_return_ns_triplet)
{
    char *text =
        "<foo:e xmlns:foo='http://expat.sf.net/' bar:a='12'\n"
        "       xmlns:bar='http://expat.sf.net/'></foo:e>";
    char *elemstr[] = {
        "http://expat.sf.net/ e foo",
        "http://expat.sf.net/ a bar"
    };
    XML_SetReturnNSTriplet(parser, 1);
    XML_SetUserData(parser, elemstr);
    XML_SetElementHandler(parser, triplet_start_checker, triplet_end_checker);
    if (!XML_Parse(parser, text, strlen(text), 1))
        xml_failure();
}
END_TEST


static Suite *
make_basic_suite(void)
{
    Suite *s = suite_create("basic");
    TCase *tc_chars = tcase_create("character tests");
    TCase *tc_attrs = tcase_create("attributes");
    TCase *tc_xmldecl = tcase_create("XML declaration");
    TCase *tc_namespace = tcase_create("XML namespaces");

    suite_add_tcase(s, tc_chars);
    tcase_add_checked_fixture(tc_chars, basic_setup, basic_teardown);
    tcase_add_test(tc_chars, test_nul_byte);
    tcase_add_test(tc_chars, test_u0000_char);
    tcase_add_test(tc_chars, test_bom_utf8);
    tcase_add_test(tc_chars, test_bom_utf16_be);
    tcase_add_test(tc_chars, test_bom_utf16_le);
    tcase_add_test(tc_chars, test_illegal_utf8);
    tcase_add_test(tc_chars, test_utf16);
    /* Regression test for SF bug #491986. */
    tcase_add_test(tc_chars, test_danish_latin1);
    /* Regression test for SF bug #514281. */
    tcase_add_test(tc_chars, test_french_charref_hexidecimal);
    tcase_add_test(tc_chars, test_french_charref_decimal);
    tcase_add_test(tc_chars, test_french_latin1);
    tcase_add_test(tc_chars, test_french_utf8);

    suite_add_tcase(s, tc_attrs);
    tcase_add_checked_fixture(tc_attrs, basic_setup, basic_teardown);
    tcase_add_test(tc_attrs, test_attr_whitespace_normalization);

    suite_add_tcase(s, tc_xmldecl);
    tcase_add_checked_fixture(tc_xmldecl, basic_setup, basic_teardown);
    tcase_add_test(tc_xmldecl, test_xmldecl_misplaced);

    suite_add_tcase(s, tc_namespace);
    tcase_add_checked_fixture(tc_namespace,
                              namespace_setup, namespace_teardown);
    tcase_add_test(tc_namespace, test_return_ns_triplet);

    return s;
}


int
main(int argc, char *argv[])
{
    int i, nf;
    int forking = 0, forking_set = 0;
    int verbosity = CK_NORMAL;
    Suite *s = make_basic_suite();
    SRunner *sr = srunner_create(s);

    /* run the tests for internal helper functions */
    testhelper_is_whitespace_normalized();

    for (i = 1; i < argc; ++i) {
        char *opt = argv[i];
        if (strcmp(opt, "-v") == 0 || strcmp(opt, "--verbose") == 0)
            verbosity = CK_VERBOSE;
        else if (strcmp(opt, "-q") == 0 || strcmp(opt, "--quiet") == 0)
            verbosity = CK_SILENT;
        else if (strcmp(opt, "-f") == 0 || strcmp(opt, "--fork") == 0) {
            forking = 1;
            forking_set = 1;
        }
        else if (strcmp(opt, "-n") == 0 || strcmp(opt, "--no-fork") == 0) {
            forking = 0;
            forking_set = 1;
        }
        else {
            fprintf(stderr, "runtests: unknown option '%s'\n", opt);
            return 2;
        }
    }
    if (forking_set)
        srunner_set_fork_status(sr, forking ? CK_FORK : CK_NOFORK);
    srunner_run_all(sr, verbosity);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    suite_free(s);

    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}