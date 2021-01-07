# typed: true
class A
  extend T::Sig

  sig do
    returns(Integer)
  end
  def foo # error: Expected `Integer` but found `NilClass` for method result type
  end
end

class Bar < A
  extend T::Sig

  sig do
    params(
      arg: Integer
    )
    .returns(Integer)
  end
  def baz(arg)
    foo
  end
end
