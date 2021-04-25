module a_01
implicit none

type :: X
    real :: r
    integer :: i
end type

contains

subroutine set(a)
type(X), intent(out) :: a
a%i = 1
a%r = 1.5
end subroutine

end module

program derived_types_01
use a_01, only: X
implicit none
type(X) :: b
b%i = 5
b%r = 3.5
!print *, b%i, b%r
! call set(b)
!print *, b%i, b%r
end
