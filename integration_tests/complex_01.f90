program complex_01
implicit none

complex :: x = (1.0,-3.0)
real, parameter :: a = 3.0, b = 4.0
complex :: y = (a, b)

complex, parameter :: i_ = (0, 1)
complex :: z = a + i_*b
complex :: w = a+b + i_*(a-b)

print *, x, y, z, w

end program
