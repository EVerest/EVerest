.. _howto_document:

##################################
How to Write EVerest Documentation
##################################

This is a short how-to for writing documentation in EVerest.

To get more detailed information about EVerest documentation, see
:ref:`Documenting EVerest <documenting_everest>`.

1. Decide where to place the documentation
  a. Module documentation goes into the module directory. You can choose
    between putting it in a docs.rst file or into a ``docs`` directory.
  b. If you want to document some partial aspects of your code (like a
    specific algorithm you use), you can add a section in the ``README.md``
    file in the corresponding GitHub repository.
  c. For documentation that is required to understand an important part or
    concept of EVerest, place the new documentation in a proper location in
    the ``docs`` directory of the
    `EVerest main repository <https://github.com/EVerest/EVerest>`_.
  d. When in doubt, use the EVerest main repository.

2. Create an issue (in case of bigger documentation changes).

  Consider to create a documentation issue inside of the
  GitHub repository you just have chosen.
  Describe the most important aspects of the topic to be documented.

3. Create a Git branch like ``doc/name-of-topic`` in the EVerest main
  repository.

  Put a note in the issue to inform the community that you start working on
  new documentation to solve that issue.

4. Create the documentation.

  You can use existing ``.rst`` files as template for creating new
  documentation pages. See this page for getting an idea how to use
  reStructuredText:
  https://www.sphinx-doc.org/en/master/usage/restructuredtext/index.html

  Also have a look at our
  :ref:`best practices page for using Sphinx in EVerest <tutorial_sphinx_style_code>`.

5. Create pull request (PR).

  After having finished your work, create a PR and set a reference to the
  originating issue (if existing).
  The maintainers of the repository will get informed automatically.
  Alternatively, you can try to find people who have the required knowledge in
  and also have the time to review your PR.
  You might find them via Zulip or the working groups.
