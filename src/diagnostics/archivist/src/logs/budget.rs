// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{container::ComponentIdentity, logs::container::LogsArtifactsContainer};
use futures::channel::mpsc;
use parking_lot::Mutex;
use std::sync::{Arc, Weak};
use tracing::{debug, error};

#[derive(Clone)]
pub struct BudgetManager {
    state: Arc<Mutex<BudgetState>>,
}

impl BudgetManager {
    pub fn new(capacity: usize) -> Self {
        Self {
            state: Arc::new(Mutex::new(BudgetState {
                capacity,
                current: 0,
                containers: vec![],
                remover: None,
            })),
        }
    }

    pub fn set_remover(&self, remover: mpsc::UnboundedSender<Arc<ComponentIdentity>>) {
        self.state.lock().remover = Some(remover);
    }

    pub fn add_container(&self, container: Arc<LogsArtifactsContainer>) {
        self.state.lock().containers.push(container);
    }

    pub fn handle(&self) -> BudgetHandle {
        BudgetHandle { state: Arc::downgrade(&self.state) }
    }
}

struct BudgetState {
    current: usize,
    capacity: usize,
    remover: Option<mpsc::UnboundedSender<Arc<ComponentIdentity>>>,

    /// Log containers are stored in a `Vec` which is regularly sorted instead of a `BinaryHeap`
    /// because `BinaryHeap`s are broken with interior mutability in the contained type which would
    /// affect the `Ord` impl's results.
    ///
    /// To use a BinaryHeap, we would have to make the container's `Ord` impl call
    /// `oldest_timestamp()`, but the value of that changes every time `pop()` is called. At the
    /// time of writing, `pop()` does not require a mutable reference. While it's only called from
    /// this module, we don't have a way to statically enforce that. This means that in a
    /// future change we could introduce incorrect and likely flakey behavior without any warning.
    containers: Vec<Arc<LogsArtifactsContainer>>,
}

impl BudgetState {
    fn allocate(&mut self, size: usize) {
        self.current += size;

        while self.current > self.capacity {
            // find the container with the oldest log message
            self.containers.sort_unstable_by_key(|c| c.oldest_timestamp().unwrap_or(std::i64::MAX));

            let container_with_oldest = self
                .containers
                .get(0)
                .expect("containers are added to budget before they can call allocate")
                .clone();
            let oldest_message = container_with_oldest
                .pop()
                .expect("if we need to free space, we have messages to remove");
            self.current -= oldest_message.metadata.size_bytes;
        }

        // now we need to remove any containers that are no longer needed. this will usually only
        // fire for components from which we've just dropped a message, but it also serves to clean
        // up containers which may not have been removable when we first received the stop event.

        // the below code is ~equivalent to the unstable drain_filter
        // https://doc.rust-lang.org/std/vec/struct.Vec.html#method.drain_filter
        let mut i = 0;
        while i != self.containers.len() {
            if !self.containers[i].should_retain() {
                let container = self.containers.remove(i);
                container.terminate();
                debug!(identity = %container.identity, "Removing now that we've popped the last message.");
                if let Some(remover) = &self.remover {
                    remover.unbounded_send(container.identity.clone()).unwrap_or_else(|err| {
                        error!(%err, identity = %container.identity, "Failed to send identity for removal");
                    });
                }
            } else {
                i += 1;
            }
        }
    }

    fn terminate(&self) {
        for container in &self.containers {
            container.terminate();
        }
    }
}

pub struct BudgetHandle {
    /// We keep a weak pointer to the budget state to avoid this ownership cycle:
    ///
    /// `BudgetManager -> BudgetState -> LogsArtifactsContainer -> BudgetHandle -> BudgetState`
    state: Weak<Mutex<BudgetState>>,
}

impl BudgetHandle {
    pub fn allocate(&self, size: usize) {
        self.state.upgrade().expect("budgetmanager outlives all containers").lock().allocate(size);
    }

    /// Terminate the log buffers of all components here in case we have some that have been
    /// removed from the data repo but we haven't dropped ourselves.
    pub fn terminate(&self) {
        self.state.upgrade().expect("budgetmanager outlives all containers").lock().terminate();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logs::{
        container::LogsArtifactsContainer,
        message::{MessageWithStats, TEST_IDENTITY},
        multiplex::PinStream,
        stats::LogStreamStats,
    };
    use diagnostics_data::Severity;
    use fidl_fuchsia_diagnostics::StreamMode;
    use futures::{Stream, StreamExt};
    use std::{
        pin::Pin,
        task::{Context, Poll},
    };

    struct CursorWrapper(PinStream<Arc<MessageWithStats>>);

    impl Stream for CursorWrapper {
        type Item = Arc<MessageWithStats>;
        fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            self.0.as_mut().poll_next(cx)
        }
    }

    #[fuchsia::test]
    async fn verify_container_is_terminated_on_removal() {
        let manager = BudgetManager::new(100);
        let container_a = Arc::new(LogsArtifactsContainer::new(
            TEST_IDENTITY.clone(),
            &vec![],
            LogStreamStats::default(),
            manager.handle(),
        ));
        let container_b = Arc::new(LogsArtifactsContainer::new(
            TEST_IDENTITY.clone(),
            &vec![],
            LogStreamStats::default(),
            manager.handle(),
        ));
        manager.add_container(container_a.clone());
        manager.add_container(container_b.clone());
        assert_eq!(manager.state.lock().containers.len(), 2);

        // Add a few test messages
        let b_message = fake_message(1);
        container_b.ingest_message(b_message.clone());
        container_a.ingest_message(fake_message(2));

        let mut cursor = CursorWrapper(container_b.cursor(StreamMode::SnapshotThenSubscribe));
        assert_eq!(cursor.next().await, Some(Arc::new(b_message)));

        container_b.mark_stopped();

        // This allocation exceeds capacity, so the B container is dropped and terminated.
        container_a.ingest_message(fake_message(3));
        assert_eq!(manager.state.lock().containers.len(), 1);

        // The container was terminated too.
        assert_eq!(container_b.buffer().final_entry(), 1);

        // Container is terminated, the cursor should give None.
        assert_eq!(cursor.next().await, None);
    }

    fn fake_message(timestamp: i64) -> MessageWithStats {
        MessageWithStats::from(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: timestamp.into(),
                component_url: Some(TEST_IDENTITY.url.clone()),
                moniker: TEST_IDENTITY.to_string(),
                severity: Severity::Debug,
                size_bytes: 50,
            })
            .set_pid(123)
            .set_tid(456)
            .build(),
        )
    }
}
