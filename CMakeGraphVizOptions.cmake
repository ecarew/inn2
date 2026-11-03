# Options for cmake --graphviz, the graph of INN's programs and the
# libraries they link:
#   cmake --graphviz=build-graphviz/inn.dot build
#   dot -Tsvg build-graphviz/inn.dot -o build-graphviz/inn.svg
# Leaves out the tests: the test_* programs, the inn_tap library they link,
# and the tcp_stdin and fake_nntp helpers (tests/CMakeLists.txt).
set(GRAPHVIZ_GRAPH_NAME "INN++")
set(GRAPHVIZ_IGNORE_TARGETS "^test_" "^inn_tap$" "^tcp_stdin$" "^fake_nntp$")
