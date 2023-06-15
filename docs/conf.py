# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys
sys.path.insert(0, os.path.abspath('../src'))
sys.path.append(os.path.abspath("./_ext"))

from sphinx.application import Sphinx

# -- Project information -----------------------------------------------------

project = 'EVerest'
copyright = '2021, Pionix GmbH'
author = 'Pionix GmbH'


# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    'sphinx.ext.autodoc', 
    'sphinx.ext.napoleon',
    'sphinx.ext.todo',
    'sphinxcontrib.contentui',
    'sphinx.ext.autosectionlabel',
    'sphinxcontrib.rsvgconverter',
    'staticpages.extension',
]

pdf_documents = [(
    'index', # main document
    u'everest-documentation', # output file
    u'EVerest', # title
    u'Pionix GmbH' # author
),]

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = 'sphinxdoc'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

master_doc = 'index'

# sphinx standard settings
file_insertion_enabled = True

# sphinx.ext.todo settings
todo_include_todos = True

# sphinx.ext.napoleon settings
napoleon_google_docstring = True
napoleon_numpy_docstring = False
napoleon_include_init_with_doc = True
napoleon_include_private_with_doc = False
napoleon_include_special_with_doc = True
napoleon_use_admonition_for_examples = False
napoleon_use_admonition_for_notes = False
napoleon_use_admonition_for_references = False
napoleon_use_ivar = False
napoleon_use_param = True
napoleon_use_rtype = True
napoleon_type_aliases = None
napoleon_attr_annotations = True

# latex settings
latex_engine = 'xelatex'
preamble=''
with open('latex_preamble.tex','r+') as f:
  preamble = f.read()
latex_elements = {
    'preamble': preamble,
}

autosectionlabel_prefix_document = True

notfound_page = {
    'pagename': '404',
    'urls_prefix': '/latest/',
}
deployed_versions = os.getenv('EVEREST_DEPLOYED_VERSIONS', "v0.0.1,v0.0.2,v0.0.3").split(',')
versionsindex_page = {
    'pagename': 'versions_index',
    'urls_prefix': '/latest/',
    'template': 'versions_index.html',
    'context': {
        'versions': deployed_versions
    }
}
staticpages_pages = [
    notfound_page,
    versionsindex_page,
]
staticpages_urls_prefix = '/latest/'
