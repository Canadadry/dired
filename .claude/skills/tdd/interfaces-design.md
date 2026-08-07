# Interface Design for Testability

Good interfaces make testing natural:

1. **Accept dependencies, don't create them**

   ```typescript
   // Testable
   function processOrder(order, paymentGateway) {}

   // Hard to test
   function processOrder(order) {
     const gateway = new StripeGateway();
   }
   ```

2. **Return results, don't produce side effects**

   ```typescript
   // Testable
   function calculateDiscount(cart): Discount {}

   // Hard to test
   function applyDiscount(cart): void {
     cart.total -= discount;
   }
   ```

3. **Small surface area**
   - Fewer methods = fewer tests needed
   - Fewer params = simpler test setup

4. **Make illegal states unrepresentable**

   Prefer precise types over generic ones. A field that's only meaningful in some cases is a sign the type is wrong, not that the field should be optional.

   ```typescript
   // Allows an impossible state: "success" with an error, or neither
   type Result = { status: string; value?: number; error?: string };

   // Illegal states unrepresentable
   type Result = { status: "success"; value: number } | { status: "error"; error: string };
   ```

5. **Keep functions pure; push statefulness to the top**

   Core logic should be pure (same input → same output, no I/O, no mutation). Confine state and side effects to a thin, high-level layer that calls into the pure core.

   ```typescript
   // Pure — no I/O, trivial to test
   function nextBoard(board: Board, move: Move): Board {}

   // Stateful — orchestrates, stays at the top
   async function handleMove(move: Move) {
     const board = await loadBoard();
     await saveBoard(nextBoard(board, move));
   }
   ```

6. **Assert preconditions at the boundary**

   Every public function that receives arguments should assert what it requires. This makes invalid input loud immediately — the error fires at the call site, not three stack frames later at the symptom.

   ```typescript
   // Unclear failure — blows up somewhere inside processOrder
   function processOrder(order, quantity) {
     // ...
   }

   // Clear failure — tells the caller exactly what went wrong
   function processOrder(order, quantity) {
     assert(order != null, "order is required");
     assert(typeof quantity === "number" && quantity > 0, `quantity must be a positive number, got ${quantity}`);
     // ...
   }
   ```

   Assertions are not input validation for end-user data — they are contracts between modules. They document what callers must guarantee and crash loudly when that contract is broken during development, pointing directly to the programmer error.

   Key invariants inside a module (e.g., "after every move the board must not contain negative values") belong as assertions too, not just as tests. Assertions are always-on self-documentation the runtime can verify.
