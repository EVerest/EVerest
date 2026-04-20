#![allow(non_snake_case)]

#[everestrs::test(config = "config_probe.yaml", module = "example_1", harness = true)]
fn test_harness_generates_counterpart(module: &Module) {
    use generated::*;
    use std::sync::mpsc::{channel, Sender};
    use std::sync::Arc;
    let (tx, rx) = channel();

    struct Dummy(Sender<()>);

    impl OnReadySubscriber for Dummy {
        fn on_ready(&self, _pub_impl: &ModulePublisher) {}
    }

    impl ExampleServiceSubscriber for Dummy {
        fn uses_something(&self, _context: &Context, _key: String) -> everestrs::Result<bool> {
            Ok(false)
        }
    }

    impl ExampleClientSubscriber for Dummy {
        fn on_max_current(&self, _context: &Context, value: f64) {
            assert_eq!(value, 123.);
            self.0.send(()).unwrap();
        }
        fn on_error_raised(
            &self,
            _context: &Context,
            _error: everestrs::ErrorType<errors::example::Error>,
        ) {
        }
        fn on_error_cleared(
            &self,
            _context: &Context,
            _error: everestrs::ErrorType<errors::example::Error>,
        ) {
        }
    }

    let dummy = Arc::new(Dummy(tx));
    let _pub = module.start(dummy.clone(), dummy.clone(), dummy.clone());

    // Wait for RsExample's on_ready to publish max_current(123.0).
    rx.recv_timeout(std::time::Duration::from_secs(5))
        .expect("Timed out waiting for on_max_current");
}

#[everestrs::harness(config = "config_probe.yaml", module = "example_1")]
mod some_module {
    use generated::*;
    use std::sync::mpsc::{channel, Sender};
    use std::sync::Arc;

    struct Dummy(Sender<()>, f64);

    impl OnReadySubscriber for Dummy {
        fn on_ready(&self, _pub_impl: &ModulePublisher) {}
    }

    impl ExampleServiceSubscriber for Dummy {
        fn uses_something(&self, _context: &Context, _key: String) -> everestrs::Result<bool> {
            Ok(false)
        }
    }

    impl ExampleClientSubscriber for Dummy {
        fn on_max_current(&self, _context: &Context, value: f64) {
            assert_eq!(value, self.1);
            self.0.send(()).unwrap();
        }
        fn on_error_raised(
            &self,
            _context: &Context,
            _error: everestrs::ErrorType<errors::example::Error>,
        ) {
        }
        fn on_error_cleared(
            &self,
            _context: &Context,
            _error: everestrs::ErrorType<errors::example::Error>,
        ) {
        }
    }

    #[everestrs::test(config = "config_probe.yaml", module = "example_1")]
    fn test_harness_in_module(module: &Module) {
        let (tx, rx) = channel();
        let dummy = Arc::new(Dummy(tx, 123.));
        let _pub = module.start(dummy.clone(), dummy.clone(), dummy.clone());

        // Wait for RsExample's on_ready to publish max_current(123.0).
        rx.recv_timeout(std::time::Duration::from_secs(5))
            .expect("Timed out waiting for on_max_current");
    }

    #[everestrs::test(config = "config_probe.yaml", module = "example_1")]
    #[should_panic]
    fn test_harness_with_panic(module: &Module) {
        let (tx, rx) = channel();
        let dummy = Arc::new(Dummy(tx, 124.));
        let _pub = module.start(dummy.clone(), dummy.clone(), dummy.clone());

        // Wait for RsExample's on_ready to publish max_current(123.0).
        rx.recv_timeout(std::time::Duration::from_secs(5))
            .expect("Timed out waiting for on_max_current");
    }
}

#[everestrs::harness(config = "config_multiple_connections.yaml", module = "probe")]
mod multiple_connections_compilation {
    use generated::*;
    use std::sync::Arc;

    struct Dummy;

    impl OnReadySubscriber for Dummy {
        fn on_ready(&self, _pub_impl: &ModulePublisher) {}
    }

    impl ExampleServiceSubscriber for Dummy {
        fn uses_something(&self, _context: &Context, _key: String) -> everestrs::Result<bool> {
            Ok(false)
        }
    }

    impl ExampleClientSubscriber for Dummy {
        fn on_max_current(&self, _context: &Context, _value: f64) {}
        fn on_error_raised(
            &self,
            _context: &Context,
            _error: everestrs::ErrorType<errors::example::Error>,
        ) {
        }
        fn on_error_cleared(
            &self,
            _context: &Context,
            _error: everestrs::ErrorType<errors::example::Error>,
        ) {
        }
    }

    #[everestrs::test(config = "config_probe.yaml", module = "example_1")]
    fn test_harness_in_module(module: &Module) {
        let dummy = Arc::new(Dummy);
        let _pub = module.start(dummy.clone(), dummy.clone(), |_a: usize| dummy.clone());
    }
}
