How To: Write tests for your module
***********************************

Introduction
__________________________
While working on your module you want to test your implemented functions. For this purpose we uses pythons unittests.
In the following you will see a simple example how to use them.


1. Create a new test file
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Go to your module directory and create a new file. This file name must start with "test_".
Maybe you want store different tests in different files. Your test file includes other python modules which lay
in various directory levels. You should put your file in the same level/directory as the highest included python module.

2. This must contain each test file
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Your test file should contain the follwing structure, it is important naming the class "Test":

.. code-block:: python
    :linenos:
    
    import unittest
    import asyncio

    class Test(unittest.IsolatedAsyncioTestCase):

3. Implement a unittest
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
After you created the TestCase you can start adding single tests, see also the `offical python doc <https://docs.python.org/3/library/unittest.html>`_.
Here you can see an example test file:

.. code-block:: python
    :linenos:

    import unittest
    import asyncio

    class Test(unittest.IsolatedAsyncioTestCase):

        def test_simple_example(self):
            self.assertTrue(True)

        def test_another_example(self):
            self.assertFalse(False)

        @unittest.expectedFailure
        def test_true_is_not_false(self):
            self.assertFalse(True)

        async def test_example_with_async(self):
            await asyncio.sleep(1)
            self.assertTrue(True)




5. Running your tests
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
A pushed test will be ran by the github action for pull requests and pushes in the main branch.
