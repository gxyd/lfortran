program matrix_matmul_01
    implicit none
    logical :: a(3, 3)
    integer :: b(3, 3)
    print *, matmul(a, b)
end program
