language core;

extend with
  #unit-type,
  #references,
  #arithmetic-operators,
  #sequencing,
  #natural-literals;

fn helper(ref : &Nat) -> fn(Nat) -> Nat {
  return
    ref := 1;
    fn(n : Nat) {
      return
        Nat::rec(n, unit, fn(i : Nat) {
          return fn(r : Unit) {
            return ref := succ(i) * (*ref)
          }
        });
        (*ref)
    }
}

fn factorial(n : Nat) -> Nat {
  return helper(new(0))(n)
}

fn main(n : Nat) -> Nat {
  return factorial(n)
}
