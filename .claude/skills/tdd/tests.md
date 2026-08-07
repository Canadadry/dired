# Good and Bad Tests

## Good Tests

**Integration-style**: Test through real interfaces, not mocks of internal parts.

```typescript
// GOOD: Tests observable behavior
test("user can checkout with valid cart", async () => {
  const cart = createCart();
  cart.add(product);
  const result = await checkout(cart, paymentMethod);
  expect(result.status).toBe("confirmed");
});
```

Characteristics:

- Tests behavior users/callers care about
- Uses public API only
- Survives internal refactors
- Describes WHAT, not HOW
- One logical assertion per test

## Bad Tests

**Implementation-detail tests**: Coupled to internal structure.

```typescript
// BAD: Tests implementation details
test("checkout calls paymentService.process", async () => {
  const mockPayment = jest.mock(paymentService);
  await checkout(cart, payment);
  expect(mockPayment.process).toHaveBeenCalledWith(cart.total);
});
```

Red flags:

- Mocking internal collaborators
- Testing private methods
- Asserting on call counts/order
- Test breaks when refactoring without behavior change
- Test name describes HOW not WHAT
- Verifying through external means instead of interface

## Assertions and Tests: Complementary Layers

Tests verify behavior from the outside. Assertions enforce contracts from the inside. Both are needed.

When production code has assertions, a failing test tells you not just _that_ something broke but _where_ the contract was violated:

```typescript
// Without assertion: test fails with a confusing symptom deep in the stack
function calculateTotal(items) {
  return items.reduce((sum, item) => sum + item.price * item.qty, 0);
}

// With assertion: test fails at the exact violated contract
function calculateTotal(items) {
  assert(Array.isArray(items), `items must be an array, got ${typeof items}`);
  items.forEach((item, i) => {
    assert(item.price >= 0, `items[${i}].price must be non-negative, got ${item.price}`);
    assert(item.qty > 0,    `items[${i}].qty must be positive, got ${item.qty}`);
  });
  return items.reduce((sum, item) => sum + item.price * item.qty, 0);
}
```

Rule of thumb: if a test setup requires calling a function with specific constraints, those constraints should be asserted inside the function. The test documents the expected behavior; the assertion enforces the contract that makes it possible.

```typescript
// BAD: Bypasses interface to verify
test("createUser saves to database", async () => {
  await createUser({ name: "Alice" });
  const row = await db.query("SELECT * FROM users WHERE name = ?", ["Alice"]);
  expect(row).toBeDefined();
});

// GOOD: Verifies through interface
test("createUser makes user retrievable", async () => {
  const user = await createUser({ name: "Alice" });
  const retrieved = await getUser(user.id);
  expect(retrieved.name).toBe("Alice");
});
```
