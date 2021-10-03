program test_bit
use lfortran_intrinsic_bit, only: iand, ior
implicit none

integer(kind=4) :: a
integer(kind=4) :: b

integer(kind=8) :: x
integer(kind=8) :: y

a = 4
b = 1
if (iand(a, b) /= 0) error stop

x = 3
y = 1
if (iand(x, y) /= 1) error stop

a = 1
b = 2
if (ior(a, b) /= 3) error stop

x = 2
y = 3
if (ior(x, y) /= 3) error stop

end program
